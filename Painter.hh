#ifndef PAINTER_HH
#define PAINTER_HH
#include <QPainter>
#include <QPrinter>
#include <QString>
#include <QImage>
#include <QFileInfo>
#include <QPageSize>
#include <QFontDatabase>
#include <QBuffer>
#include <QMutexLocker>
#include <QTimer>

struct PDFSettings {
    QImage::Format colorFormat;
    Qt::ImageConversionFlags conversionFlags;
    enum Compression { CompressZip, CompressFax4, CompressJpeg } compression;
    int compressionQuality;
    QString fontFamily;
    int fontSize;
    bool uniformizeLineSpacing;
    int preserveSpaceWidth;
    bool overlay;
    double detectedFontScaling;
};

class PDFPainter {
public:
    virtual void setFontFamily(const QString& family, bool bold, bool italic) = 0;
    virtual void setFontSize(double pointSize) = 0;
    virtual void drawText(double x, double y, const QString& text) = 0;
    virtual void drawImage(const QRect& bbox, const QImage& image, const PDFSettings& settings) = 0;
    virtual double getAverageCharWidth() const = 0;
    virtual double getTextWidth(const QString& text) const = 0;
    virtual bool createPage(double /*width*/, double /*height*/, double /*offsetX*/, double /*offsetY*/, QString& /*errMsg*/) { return true; }
    virtual void finishPage() {}
    virtual bool finishDocument(QString& /*errMsg*/){ return true; }
protected:
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
    QVector<QRgb> createGray8Table() const {
        QVector<QRgb> colorTable(255);
        for(int i = 0; i < 255; ++i) {
            colorTable[i] = qRgb(i, i, i);
        }
        return colorTable;
    }
#endif
    QImage convertedImage(const QImage& image, QImage::Format targetFormat, Qt::ImageConversionFlags flags) const {
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
        if(image.format() == targetFormat) {
            return image;
        } else if(targetFormat == QImage::Format_Indexed8) {
            static QVector<QRgb> gray8Table = createGray8Table();
            return image.convertToFormat(targetFormat, gray8Table);
        } else {
            return image.convertToFormat(targetFormat);
        }
#else
        return image.format() == targetFormat ? image : image.convertToFormat(targetFormat, flags);
#endif
    }
};

class QPainterPDFPainter : public PDFPainter {
public:
    QPainterPDFPainter(QPainter* painter, const QFont& defaultFont)
        : m_painter(painter), m_defaultFont(defaultFont) {
        m_curFont = m_defaultFont;
    }
    void setFontFamily(const QString& family, bool bold, bool italic) override {
        float curSize = m_curFont.pointSize();
        if(m_fontDatabase.hasFamily(family)) {
            m_curFont.setFamily(family);
        }  else {
            m_curFont = m_defaultFont;
        }
        m_curFont.setPointSize(curSize);
        m_curFont.setBold(bold);
        m_curFont.setItalic(italic);
        m_painter->setFont(m_curFont);
    }
    void setFontSize(double pointSize) override {
        if(pointSize != m_curFont.pointSize()) {
            m_curFont.setPointSize(pointSize);
            m_painter->setFont(m_curFont);
        }
    }
    void drawText(double x, double y, const QString& text) override {
        m_painter->drawText(m_offsetX + x, m_offsetY + y, text);
    }
    void drawImage(const QRect& bbox, const QImage& image, const PDFSettings& settings) override {
        QImage img = convertedImage(image, settings.colorFormat, settings.conversionFlags);
        if(settings.compression == PDFSettings::CompressJpeg) {
            QByteArray data;
            QBuffer buffer(&data);
            img.save(&buffer, "jpg", settings.compressionQuality);
            img = QImage::fromData(data);
        }
        m_painter->drawImage(bbox, img);
    }
    double getAverageCharWidth() const override {
        return m_painter->fontMetrics().averageCharWidth();
    }
    double getTextWidth(const QString& text) const override {
        return m_painter->fontMetrics().width(text);
    }
protected:
    QFontDatabase m_fontDatabase;
    QPainter* m_painter;
    QFont m_curFont;
    QFont m_defaultFont;
    double m_offsetX = 0.0;
    double m_offsetY = 0.0;
};

class QPrinterPDFPainter : public QPainterPDFPainter {
public:
    QPrinterPDFPainter(const QString& filename, const QString& creator, const QFont& defaultFont)
        : QPainterPDFPainter(nullptr, defaultFont) {
        m_printer.setOutputFormat(QPrinter::PdfFormat);
        m_printer.setOutputFileName(filename);
        m_printer.setResolution(72); // This to ensure that painter units are points - images are passed pre-scaled to drawImage, so that the resulting dpi in the box is the output dpi (the document resolution only matters for images)
        m_printer.setFontEmbeddingEnabled(true);
        m_printer.setFullPage(true);
        m_printer.setCreator(creator);
    }
    ~QPrinterPDFPainter() {
        delete m_painter;
    }
    bool createPage(double width, double height, double offsetX, double offsetY, QString& errMsg) override {
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
        m_printer.setPaperSize(width, pageDpi), QPrinter::Point);
#else
        m_printer.setPageSize(QPageSize(QSizeF(width, height), QPageSize::Point));
#endif
        if(!m_firstPage) {
        if(!m_printer.newPage()) {
                return false;
            }
        } else {
            // Need to create painter after setting paper size, otherwise default paper size is used for first page...
            m_painter = new QPainter();
            m_painter->setRenderHint(QPainter::Antialiasing);
            if(!m_painter->begin(&m_printer)) {
                errMsg = QString("Failed to write to file");
                return false;
            }
            m_firstPage = false;
        }
        m_curFont = m_defaultFont;
        m_painter->setFont(m_curFont);
        m_offsetX = offsetX;
        m_offsetY = offsetY;
        return true;
    }
    bool finishDocument(QString& /*errMsg*/) override {
        return m_painter->end();
    }
    void drawImage(const QRect& bbox, const QImage& image, const PDFSettings& settings) override {
        QImage img = convertedImage(image, settings.colorFormat, settings.conversionFlags);
        m_painter->drawImage(bbox, img);
    }

private:
    QPrinter m_printer;
    bool m_firstPage = true;
};

#endif // PAINTER_HH
