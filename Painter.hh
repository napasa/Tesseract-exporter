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
        m_curFont = painter->font();
    }
    void setFontFamily(const QString& family, bool bold, bool italic) override {
        if(m_fontDatabase.hasFamily(family)) {
            m_curFont.setFamily(family);
        }  else {
            m_curFont = m_defaultFont;
        }
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
        m_painter->drawText(x, y, text);
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

private:
    QFontDatabase m_fontDatabase;
    QPainter* m_painter;
    QFont m_curFont;
    QFont m_defaultFont;
};


#endif // PAINTER_HH
