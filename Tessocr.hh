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

class PdfOcrParam{
public:
    PdfOcrParam(const QString &password, const QString &lang,
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
        return m_interProcessInfo->m_cancle;
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
    enum OUTFILE_TYPE{
        PDF,
        XML,
        TXT
    };
public:
    TessOcr(const QString &parentOfTessdataDir);
    int OcrPdf(const QString &infile, const PdfOcrParam &pdfOcrParam, ProgressInfo *interProcessInfo);
    int ExportPdf(const QString& outname, ProgressInfo *interProcessInfo);
    int ExporteXML(const QString& outname, ProgressInfo *interProcessInfo);
    int ExportTxt(const QString& outname, ProgressInfo *interProcessInfo);
    void SetOutfileType(OUTFILE_TYPE outfileType){ m_outfileType = outfileType;}
    OUTFILE_TYPE GetOutfileType(){ return m_outfileType;}
private:
    QList<QImage> GetOCRAreas(const QFileInfo &fileinfo, int resolution, int page);
    void ProcessPdf(const char *hocrtext, PageData pageData);
    QPageSize GetPdfPageSize(const HOCRDocument *hocrdocument);
    void PrintChildren(PDFPainter& painter, const HOCRItem* item, const PDFSettings& pdfSettings, double imgScale);
    int ExportResult(const QString& outname, ProgressInfo *interProgressInfo);
    PDFSettings &GetPdfSettings();
private:
    HOCRDocument m_hocrDocument;
    QString m_parentOfTessdataDir;
    QString m_utf8Text;
    OUTFILE_TYPE m_outfileType;
    PDFSettings m_pdfSettings;
};

#endif // TESSOCR_H
