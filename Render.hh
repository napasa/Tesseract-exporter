#ifndef RENDER_H
#define RENDER_H
#include <QByteArray>
#include <QString>
#include <QMutex>

class QImage;
namespace Poppler {
class Document;
}

class DisplayRenderer {
public:
    DisplayRenderer(const QString& filename) : m_filename(filename) {}
    virtual ~DisplayRenderer() {}
    virtual QImage render(int page, double resolution) const = 0;
    virtual int getNPages() const = 0;

    void adjustImage(QImage& image, int brightness, int contrast, bool invert) const;

protected:
    QString m_filename;
};

class ImageRenderer : public DisplayRenderer {
public:
    ImageRenderer(const QString& filename) ;
    QImage render(int page, double resolution) const override;
    int getNPages() const override {
        return m_pageCount;
    }
private:
    int m_pageCount;
};

class PDFRenderer : public DisplayRenderer {
public:
    PDFRenderer(const QString& filename, const QByteArray& password);
    ~PDFRenderer();
    QImage render(int page, double resolution) const override;
    int getNPages() const override;

private:
    Poppler::Document* m_document;
    mutable QMutex m_mutex;
};


#endif // RENDER_H
