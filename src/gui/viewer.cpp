
#include <QGroupBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QGridLayout>
#include <QDockWidget>
#include <QPainter>
#include <QHeaderView>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QScrollArea>
#include <QMessageBox>
#include <QCloseEvent>
#include <QFileDialog>

#include <iostream>

#include "fmt/format.h"

#include "viewer.hpp"

#include "common/util.hpp"
#include "common/enums.hpp"

using namespace vtfview;
using namespace VTFLib;

//////////////////////////////////////////////////////////////////////////////////
// ViewerMainWindow
//////////////////////////////////////////////////////////////////////////////////

ViewerMainWindow::ViewerMainWindow(QWidget* pParent) :
	QMainWindow(pParent) {
	setup_ui();
}


bool ViewerMainWindow::load_file(const char* path) {
	std::uint8_t* buffer;
	auto numRead = util::read_file(path, buffer);
	if (!numRead)
		return false;
		
	bool ok = load_file(buffer, numRead);
	delete buffer;
	
	setWindowTitle(
		fmt::format(FMT_STRING("VTFView - [{}]"), str::get_filename(path)).c_str()
	);

	path_ = path;
	return ok;
}

bool ViewerMainWindow::load_file(const void* data, size_t size) {
	file_ = new VTFLib::CVTFFile();
	if (!file_->Load(data, size)) {
		delete file_;
		file_ = nullptr;
		return false;
	}
	return load_file(file_);
}

bool ViewerMainWindow::load_file(VTFLib::CVTFFile* file) {
	emit vtfFileChanged(file);
	file_ = file;
	path_ = "";
	return true;
}

void ViewerMainWindow::unload_file() {
	if (!file_)
		return;
	emit vtfFileChanged(nullptr);
	delete file_;
	file_ = nullptr;
	path_ = "";
}

void ViewerMainWindow::setup_ui() {
	setWindowTitle("VTFView");
	
	setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::North);
	setTabPosition(Qt::RightDockWidgetArea, QTabWidget::North);
	
	// Info widget
	auto* infoDock = new QDockWidget(tr("Info"), this);
	
	auto* infoWidget = new InfoWidget(this);
	connect(this, &ViewerMainWindow::vtfFileChanged, infoWidget, &InfoWidget::update_info);

	infoDock->setWidget(infoWidget);
	addDockWidget(Qt::RightDockWidgetArea, infoDock);
	
	// Resource list
	auto* resDock = new QDockWidget(tr("Resources"), this);
	
	auto* resList = new ResourceWidget(this);
	connect(this, &ViewerMainWindow::vtfFileChanged, resList, &ResourceWidget::set_vtf);
	
	resDock->setWidget(resList);
	addDockWidget(Qt::RightDockWidgetArea, resDock);
		
	// Main image viewer 
	auto* imageView = new ImageViewWidget(this);
	
	connect(this, &ViewerMainWindow::vtfFileChanged, [imageView](VTFLib::CVTFFile* file) {
		imageView->set_vtf(file);
	});
	setCentralWidget(imageView);
	
	// Viewer settings
	auto* viewerDock = new QDockWidget(tr("Viewer Settings"), this);
	
	auto* viewSettings = new ImageSettingsWidget(imageView, this);
	connect(this, &ViewerMainWindow::vtfFileChanged, viewSettings, &ImageSettingsWidget::set_vtf);
	connect(viewSettings, &ImageSettingsWidget::fileModified, this, &ViewerMainWindow::mark_modified);
	
	viewerDock->setWidget(viewSettings);
	addDockWidget(Qt::LeftDockWidgetArea, viewerDock);
	
	// Tabify the docks 
	tabifyDockWidget(infoDock, resDock);
}

void ViewerMainWindow::reset_state() {
	dirty_ = false;
}

void ViewerMainWindow::mark_modified() {
	dirty_ = true;
	
	auto title = windowTitle();
	if (title.endsWith("*"))
		return;
	setWindowTitle(title + "*");
}

void ViewerMainWindow::save() {
	if (!dirty_)
		return;
	dirty_ = false;
	
	// Ask for a save directory if there's no active file
	if (path_.empty()) {
		auto name = QFileDialog::getSaveFileName(this, tr("Save as"), QString(), "Valve Texture File (*.vtf)");
		if (name.isEmpty())
			return;
		path_ = name.toUtf8().data();
	}
	
	if (!file_->Save(path_.c_str())) {
		QMessageBox::warning(this, "Could not save file!",
			fmt::format(FMT_STRING("Failed to save file: {}"), vlGetLastError()).c_str(),
			QMessageBox::Ok);
		return;
	}
	
	// Clear out the window asterick
	auto title = windowTitle();
	title.remove(title.length()-1, 1);
	setWindowTitle(title);
}

void ViewerMainWindow::closeEvent(QCloseEvent* event) {
	if (dirty_) {
		auto msgBox = new QMessageBox(QMessageBox::Icon::Question, tr("Quit without saving?"), tr("You have unsaved changes. Would you like to save?"), 
			QMessageBox::NoButton, this);
		msgBox->addButton(QMessageBox::Save);
		msgBox->addButton(QMessageBox::Cancel);
		msgBox->addButton(QMessageBox::Close);
		auto r = msgBox->exec();
		
		if (r == QMessageBox::Cancel) {
			event->ignore(); // Just eat the event
			return;
		}
		else if (r == QMessageBox::Save) {
			save();
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////
// InfoWidget
//////////////////////////////////////////////////////////////////////////////////

static inline constexpr const char* INFO_FIELDS[] = {
	"Width", "Height", "Depth",
	"Frames", "Faces", "Mips",
	"Image format",
	"Reflectivity"
};

static inline constexpr const char* FILE_FIELDS[] = {
	"Size", "Version"
};

InfoWidget::InfoWidget(QWidget* pParent) :
	QWidget(pParent) {
	setup_ui();
}

void InfoWidget::update_info(VTFLib::CVTFFile* file) {
	find("Width")->setText(QString::number(file->GetWidth()));
	find("Height")->setText(QString::number(file->GetHeight()));
	find("Depth")->setText(QString::number(file->GetDepth()));
	find("Frames")->setText(QString::number(file->GetFrameCount()));
	find("Faces")->setText(QString::number(file->GetFaceCount()));
	find("Mips")->setText(QString::number(file->GetMipmapCount()));
	find("Image format")->setText(ImageFormatToString(file->GetFormat()));
	
	find("Version")->setText(QString::number(file->GetMajorVersion()) + "." + QString::number(file->GetMinorVersion()));
	auto size = file->GetSize();
	find("Size")->setText(
		fmt::format(FMT_STRING("{:.2f} MiB ({:.2f} KiB)"), size / (1024.f*1024.f), size / 1024.f).c_str()
	);
	
	vlSingle x, y, z;
	file->GetReflectivity(x, y, z);
	find("Reflectivity")->setText(
		fmt::format(FMT_STRING("{:.3f} {:.3f} {:.3f}"), x, y, z).c_str()
	);
}

void InfoWidget::setup_ui() {
	auto* layout = new QVBoxLayout(this);
	auto* fileGroupBox = new QGroupBox(tr("File Metadata"), this);
	auto* imageGroupBox = new QGroupBox(tr("Image Info"), this);
	
	auto* fileGroupLayout = new QGridLayout(fileGroupBox);
	auto* imageGroupLayout = new QGridLayout(imageGroupBox);
	fileGroupLayout->setColumnStretch(1, 1);
	imageGroupLayout->setColumnStretch(1, 1);
	
	// Prevent rows from expanding on resize
	fileGroupLayout->setRowStretch(util::ArraySize(FILE_FIELDS), 1);
	imageGroupLayout->setRowStretch(util::ArraySize(INFO_FIELDS), 1);
	
	// File meta info
	int row = 0;
	for (auto& f : FILE_FIELDS) {
		auto* label = new QLabel(QString(f) + ":", fileGroupBox);
		auto* edit = new QLineEdit(this);
		edit->setReadOnly(true);
		
		fileGroupLayout->addWidget(label, row, 0);
		fileGroupLayout->addWidget(edit, row, 1);
		++row;
		
		fields_.insert({f, edit});
	}
	
	// Image contents info
	row = 0;
	for (auto& f : INFO_FIELDS) {
		auto* label = new QLabel(QString(f) + ":", imageGroupBox);
		auto* edit = new QLineEdit(this);
		edit->setReadOnly(true);
		
		imageGroupLayout->addWidget(label, row, 0);
		imageGroupLayout->addWidget(edit, row, 1);
		++row;
		
		fields_.insert({f, edit});
	}
	
	layout->addWidget(fileGroupBox);
	layout->addWidget(imageGroupBox);
	
	// Prevent space being added to the bottom of the file metadata group box
	layout->addStretch(1);
}

//////////////////////////////////////////////////////////////////////////////////
// ImageViewWidget
//////////////////////////////////////////////////////////////////////////////////

ImageViewWidget::ImageViewWidget(QWidget* pParent) :
	QWidget(pParent) {
	setMinimumSize(256, 256);
}
		
void ImageViewWidget::set_pixmap(const QImage& pixmap) {
	image_ = pixmap;
}

void ImageViewWidget::set_vtf(VTFLib::CVTFFile* file) {
	file_ = file;
	// Force refresh of data
	currentFrame_ = -1;
	currentFace_ = -1;
	currentMip_ = -1;
	
	zoom_ = 1.f;
	pos_ = {0,0};
	
	// Make the image fit if it doesn't
	QSize sz(file->GetWidth(), file->GetHeight());
	if (size().width() < sz.width() || size().height() < sz.height())
		resize(sz);
}

void ImageViewWidget::paintEvent(QPaintEvent* event) {
	QPainter painter(this);
	
	// Compute draw size for this mip, frame, etc
	vlUInt imageWidth, imageHeight, imageDepth;
	CVTFFile::ComputeMipmapDimensions(file_->GetWidth(), file_->GetHeight(), file_->GetDepth(), mip_, imageWidth, imageHeight, imageDepth);
		
	// Needs decode
	if (frame_ != currentFrame_ || mip_ != currentMip_ || face_ != currentFace_) {
		const bool hasAlpha = CVTFFile::GetImageFormatInfo(file_->GetFormat()).uiAlphaBitsPerPixel > 0;
		const VTFImageFormat format = hasAlpha ? IMAGE_FORMAT_RGBA8888 : IMAGE_FORMAT_RGB888;
		auto size = file_->ComputeMipmapSize(file_->GetWidth(), file_->GetHeight(), 1, mip_, format);
		
		if (imgBuf_) {
			free(imgBuf_);
		}
		// This buffer needs to persist- QImage does not own the mem you give it
		imgBuf_ = static_cast<vlByte*>(malloc(size));

		bool ok = CVTFFile::Convert(file_->GetData(frame_, face_, 0, mip_), (vlByte*)imgBuf_, imageWidth, imageHeight, file_->GetFormat(), format);
			
		if (!ok) {
			std::cerr << "Could not convert image for display.\n";
			return;
		}
		
		image_ = QImage((uchar*)imgBuf_, imageWidth, imageHeight, hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
		
		currentFace_ = face_;
		currentFrame_ = frame_;
		currentMip_ = mip_;
	}
	
	QPoint center = QPoint(width()/2, height()/2) - QPoint(imageWidth/2, imageHeight/2);
	painter.drawImage(center + pos_, image_);
}

//////////////////////////////////////////////////////////////////////////////////
// ResourceWidget
//////////////////////////////////////////////////////////////////////////////////

ResourceWidget::ResourceWidget(QWidget* parent) :
	QWidget(parent) {
	setup_ui();
}

void ResourceWidget::set_vtf(VTFLib::CVTFFile* file) {
	table_->clear();
	
	auto resources = file->GetResourceCount();
	table_->setRowCount(resources);
	for (vlUInt i = 0; i < resources; ++i) {
		auto type = file->GetResourceType(i);
		vlUInt size;
		auto data = file->GetResourceData(type, size);
		
		table_->setItem(i, 0, new QTableWidgetItem(GetResourceName(type)));
		
		auto typeItem = new QTableWidgetItem(
			fmt::format(FMT_STRING("0x{:X}"), type).c_str()
		);
		table_->setItem(i, 1, typeItem);
		
		auto sizeItem = new QTableWidgetItem(
			fmt::format(FMT_STRING("{:d} bytes ({:.2f} KiB)"), size, size / 1024.f).c_str()
		);
		table_->setItem(i, 2, sizeItem);
	}
}

void ResourceWidget::setup_ui() {
	auto* layout = new QVBoxLayout(this);
	
	table_ = new QTableWidget(this);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->verticalHeader()->hide();
	table_->setColumnCount(3);
	table_->horizontalHeader()->setStretchLastSection(true);
	table_->setHorizontalHeaderItem(0, new QTableWidgetItem("Resource Name"));
	table_->setHorizontalHeaderItem(1, new QTableWidgetItem("Resource Type"));
	table_->setHorizontalHeaderItem(2, new QTableWidgetItem("Data Size"));
	
	layout->addWidget(table_);
}

//////////////////////////////////////////////////////////////////////////////////
// Texture flag list
//////////////////////////////////////////////////////////////////////////////////

constexpr struct TextureFlag {
	uint32_t flag;
	const char* name;
} TEXTURE_FLAGS[] = {
	{TEXTUREFLAGS_POINTSAMPLE, "Point Sample"},
	{TEXTUREFLAGS_TRILINEAR, "Trilinear"},
	{TEXTUREFLAGS_CLAMPS, "Clamp S"},
	{TEXTUREFLAGS_CLAMPT, "Clamp T"},
	{TEXTUREFLAGS_CLAMPU, "Clamp U"},
	{TEXTUREFLAGS_ANISOTROPIC, "Anisotropic"},
	{TEXTUREFLAGS_HINT_DXT5, "Hint DXT5"},
	{TEXTUREFLAGS_SRGB, "sRGB"},
	{TEXTUREFLAGS_DEPRECATED_NOCOMPRESS, "Nocompress (Deprecated)"},
	{TEXTUREFLAGS_NORMAL, "Normal"},
	{TEXTUREFLAGS_NOMIP, "No MIP"},
	{TEXTUREFLAGS_NOLOD, "No LOD"},
	{TEXTUREFLAGS_MINMIP, "Min Mip"},
	{TEXTUREFLAGS_PROCEDURAL, "Procedural"},
	{TEXTUREFLAGS_ONEBITALPHA, "One-bit Alpha"},
	{TEXTUREFLAGS_EIGHTBITALPHA, "Eight-bit Alpha"},
	{TEXTUREFLAGS_ENVMAP, "Envmap"},
	{TEXTUREFLAGS_RENDERTARGET, "Render Target"},
	{TEXTUREFLAGS_DEPTHRENDERTARGET, "Depth Render Target"},
	{TEXTUREFLAGS_NODEBUGOVERRIDE, "No Debug Override"},
	{TEXTUREFLAGS_SINGLECOPY, "Single Copy"},
	{TEXTUREFLAGS_DEPRECATED_ONEOVERMIPLEVELINALPHA, "One Over Mip Level Linear Alpha (Deprecated)"},
	{TEXTUREFLAGS_DEPRECATED_PREMULTCOLORBYONEOVERMIPLEVEL, "Pre-multiply Colors by One Over Mip Level (Deprecated)"},
	{TEXTUREFLAGS_DEPRECATED_NORMALTODUDV, "Normal To DuDv"},
	{TEXTUREFLAGS_DEPRECATED_ALPHATESTMIPGENERATION, "Alpha Test Mip Generation (Deprecated)"},
	{TEXTUREFLAGS_NODEPTHBUFFER, "No Depth Buffer"},
	{TEXTUREFLAGS_DEPRECATED_NICEFILTERED, "Nice Filtered (Deprecated)"},
	{TEXTUREFLAGS_VERTEXTEXTURE, "Vertex Texture"},
	{TEXTUREFLAGS_SSBUMP, "SSBump"},
	{TEXTUREFLAGS_DEPRECATED_UNFILTERABLE_OK, "Unfilterable OK (Deprecated)"},
	{TEXTUREFLAGS_BORDER, "Border"},
	{TEXTUREFLAGS_DEPRECATED_SPECVAR_RED, "Specvar Red (Deprecated)"},
	{TEXTUREFLAGS_DEPRECATED_SPECVAR_ALPHA, "Specvar Alpha (Deprecated)"},
};


//////////////////////////////////////////////////////////////////////////////////
// ImageSettingsWidget
//////////////////////////////////////////////////////////////////////////////////
ImageSettingsWidget::ImageSettingsWidget(ImageViewWidget* viewer, QWidget* parent) :
	QWidget(parent) {
	setup_ui(viewer);
}

void ImageSettingsWidget::setup_ui(ImageViewWidget* viewer) {
	auto* layout = new QGridLayout(this);

	int row = 0;
	frame_ = new QSpinBox(this);
	connect(frame_, &QSpinBox::textChanged, [viewer, this](const QString&) {
		viewer->set_frame(frame_->value());
	});
	layout->addWidget(frame_, row, 1);
	layout->addWidget(new QLabel("Frame:"), row, 0);

	++row;
	mip_ = new QSpinBox(this);
	connect(mip_, &QSpinBox::textChanged, [viewer, this](const QString&) {
		viewer->set_mip(mip_->value());
	});
	layout->addWidget(mip_, row, 1);
	layout->addWidget(new QLabel("Mip:"), row, 0);
	
	++row;
	face_ = new QSpinBox(this);
	connect(face_, &QSpinBox::textChanged, [viewer, this](const QString&) {
		viewer->set_face(face_->value());
	});
	layout->addWidget(face_, row, 1);
	layout->addWidget(new QLabel("Face:"), row, 0);
	
	++row;
	startFrame_ = new QSpinBox(this);
	connect(startFrame_, &QSpinBox::textChanged, [viewer, this](const QString&) {
		if (!file_)
			return;
		file_->SetStartFrame(startFrame_->value());
		if (!settingFile_)
			emit fileModified();
	});
	layout->addWidget(startFrame_, row, 1);
	layout->addWidget(new QLabel("Start Frame:"), row, 0);
	
	// Flags list box
	++row;
	auto* flagsScroll = new QScrollArea(this);
	auto* flagsGroup = new QGroupBox(tr("Flags"), this);
	auto* flagsLayout = new QGridLayout(flagsGroup);
	
	for (auto& flag : TEXTURE_FLAGS) {
		auto* check = new QCheckBox(flag.name, this);
		check->setCheckable(true);
		connect(check, &QCheckBox::stateChanged, [this, flag](int newState) {
			if (!file_)
				return;
			file_->SetFlag((VTFImageFlag)flag.flag, newState);
			if (!settingFile_)
				emit fileModified();
		});
		flagChecks_.insert({flag.flag, check});
		flagsLayout->addWidget(check);
	}
	
	flagsScroll->setWidget(flagsGroup);
	layout->addWidget(flagsScroll, row, 0, 1, 2);
}

void ImageSettingsWidget::set_vtf(VTFLib::CVTFFile* file) {
	// Hack to ensure we don't emit fileModified when setting defaults
	settingFile_ = true;
	
	file_ = file;
	startFrame_->setValue(file->GetStartFrame());
	mip_->setValue(0);
	frame_->setValue(file->GetStartFrame());
	face_->setValue(0);
	
	// Configure ranges
	mip_->setRange(0, file->GetMipmapCount());
	frame_->setRange(1, file->GetFrameCount());
	face_->setRange(1, file->GetFaceCount());
	startFrame_->setRange(1, file->GetFrameCount());
	
	// Set the flags
	uint32_t flags = file->GetFlags();
	for (auto& f : TEXTURE_FLAGS) {
		flagChecks_.find(f.flag)->second->setChecked(!!(flags & f.flag));
	}
	
	settingFile_ = false;
}
