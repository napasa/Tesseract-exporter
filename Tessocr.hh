#ifndef TESSOCR_H
#define TESSOCR_H

#include <tesseract/baseapi.h>
#include <tesseract/ocrclass.h>
#include <tesseract/strngs.h>
#include <tesseract/genericvector.h>
#include "Painter.hh"
#include "HOCRDocument.hh"
#include "Interprocess.hh"

struct PageData {
    bool success;
    QString filename;
    int page;
    double angle;
    int resolution;
    QList<QImage> ocrAreas;
};

class OcrParam{
public:
    OcrParam(){}
    OcrParam(const QString &password, const QString &lang,
                const QList<int> &pages, const PdfPostProcess &pdfPostProcess);
    QString m_password;
    QString m_lang;
    QList<int> m_pages;
    PdfPostProcess m_pdfPostProcess;
};


class AbstractProgressMonitor {
public:
    AbstractProgressMonitor(int total) : m_total(total) {}
    virtual ~AbstractProgressMonitor() {}
    int increaseProgress() {
        ++m_pageProgress;
        return m_pageProgress;
    }
protected:
    const int m_total;
    int m_pageProgress = 0;
};


class ProgressMonitor : public AbstractProgressMonitor {
public:
    ETEXT_DESC desc;
    ProgressMonitor(int nPages, ProgressInfo*interProgressInfo)
        : AbstractProgressMonitor(nPages), m_interProcessInfo(interProgressInfo){
        desc.progress = 0;
        desc.cancel = CancelCallback;
        desc.cancel_this = this;
    }
    static bool CancelCallback(void* instance, int /*words*/) {
        static int lastProcess=0;
        ProgressMonitor* monitor = reinterpret_cast<ProgressMonitor*>(instance);
        int progress = monitor->GetProgress();
        if(lastProcess != progress){
            lastProcess = progress;
            monitor->m_interProcessInfo->m_progress = progress*0.9;
        }
        return monitor->Cancelled();
    }
    bool Cancelled(){
        return m_interProcessInfo->m_errCode==ERROR_CODE::CANCLED_BY_USER;
    }
private:
    ProgressInfo *m_interProcessInfo;
public slots:
    int GetProgress() const {
        return 100.0 * ((m_pageProgress + desc.progress / 99.0) / m_total);
    }
};

class TessOcr
{
public:
    enum FILE_TYPE{
        PDF,
        XML,
        TXT,
        IMG
    };
public:
    TessOcr(const QString &parentOfTessdataDir);
    ERROR_CODE recognize(const QString &inPath, const OcrParam &pdfOcrParam, bool autodetectLayout, ProgressInfo *interProcessInfo);
    ERROR_CODE ParseXML(const QString &inPath, ProgressInfo *interProcessInfo);

    ERROR_CODE ExportPdf(const QString& outPath, ProgressInfo *interProcessInfo);
    ERROR_CODE ExporteXML(const QString& outPath, ProgressInfo *interProcessInfo);
    ERROR_CODE ExportTxt(const QString& outPath, ProgressInfo *interProcessInfo);

    void SetOutfileType(FILE_TYPE outfileType){ m_outfileType = outfileType;}
    FILE_TYPE GetOutfileType(){ return m_outfileType;}
    void SetInfileType(FILE_TYPE infileType){m_infileType = infileType;}
    FILE_TYPE GetInfileType(){ return m_infileType;}
private:
    QList<QImage> GetOCRAreas(const QFileInfo &fileinfo, int resolution, int page);
    void read(const char *hocrtext, PageData pageData);
    QPageSize GetPdfPageSize(const HOCRDocument *hocrdocument);
    ERROR_CODE ExportResult(const QString& outPath, ProgressInfo *interProgressInfo);
    PDFSettings &GetPdfSettings();
    ERROR_CODE CheckFileStatus(const QFileInfo &fileInfo, ProgressInfo *interProcessInfo, const OcrParam &pdfOcrParam=OcrParam());
private:
    void printChildren(PDFPainter& painter, const HOCRItem* item, const PDFSettings& pdfSettings, double px2pu, double imgScale = 1.);
    PDFSettings getPdfSettings() const;
    PageData setPage(int page, bool autodetectLayout, QString filename);

    HOCRDocument m_hocrDocument;
    QString m_parentOfTessdataDir;
    QString m_utf8Text;
    FILE_TYPE m_outfileType;
    FILE_TYPE m_infileType;
    PDFSettings m_pdfSettings;

    PageData m_pageData;
};

#endif // TESSOCR_H
