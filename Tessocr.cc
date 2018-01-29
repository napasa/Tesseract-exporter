#include "Tessocr.hh"
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
#include <poppler-qt4.h>
#else
#include <poppler-qt5.h>
#endif
#include <fstream>
#include <thread>
#ifdef DEBUG
#include <iostream>
#endif
#include "HOCRDocument.hh"
#include "Render.hh"


PdfOcrParam::PdfOcrParam(const QString &password, const QString &lang, const QList<int> &pages)
    :m_lang(lang), m_pages(pages){
}

struct ReadSessionData {
    virtual ~ReadSessionData() = default;
    bool prependFile;
    bool prependPage;
    int page;
    QString file;
    double angle;
    int resolution;
};



ReadSessionData *initRead(tesseract::TessBaseAPI &tess)
{
    tess.SetPageSegMode(tesseract::PSM_AUTO_ONLY);
    return new ReadSessionData;
}

QRectF GetSceneBoundingRect(QPixmap pixmap){
    // We cannot use m_imageItem->sceneBoundingRect() since its pixmap
    // can currently be downscaled and therefore have slightly different
    // proportions.
    int width = pixmap.width();
    int height = pixmap.height();
    QRectF rect(width * -0.5, height * -0.5, width, height);
    QTransform transform;
    transform.rotate(0);
    return transform.mapRect(rect);
}

QImage GetImage(const QRectF& rect, QPixmap pixmap) {
    QImage image(rect.width(), rect.height(), QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QTransform t;
    t.translate(-rect.x(), -rect.y());
    t.rotate(0);
    t.translate(-0.5 * pixmap.width(), -0.5 * pixmap.height());
    painter.setTransform(t);
    painter.drawPixmap(0, 0, pixmap);
    return image;
}

QList<QImage> TessOcr::GetOCRAreas(const QFileInfo &fileinfo, int resolution, int page) {
    PDFRenderer pdfRender(fileinfo.filePath(),"");
    QImage image = pdfRender.render(page, resolution);
    QPixmap pixmap = QPixmap::fromImage(image);
    QRectF rect = GetSceneBoundingRect(pixmap);
    QImage processedImage = GetImage(rect, pixmap);
    return QList<QImage>() << processedImage;
}

QPageSize TessOcr::GetPdfPageSize(const HOCRDocument*hocrdocument){
    double pageWidth, pageHeight;
    const HOCRPage* page = hocrdocument->page(0);
    pageWidth = double(page->bbox().width())/page->resolution()*72;
    pageHeight = double(page->bbox().height())/page->resolution()*72;
    return QPageSize(QSize(pageWidth, pageHeight), "custom",QPageSize::ExactMatch);
}

PDFSettings GetPdfSettings() {
    PDFSettings pdfSettings;
    pdfSettings.colorFormat = QImage::Format_RGB888 ;
    pdfSettings.conversionFlags = Qt::AutoColor;
    pdfSettings.compression = PDFSettings::Compression::CompressJpeg;
    pdfSettings.compressionQuality = 90;
    pdfSettings.fontFamily = "";
    pdfSettings.fontSize = -1;
    pdfSettings.uniformizeLineSpacing = true;
    pdfSettings.preserveSpaceWidth = 1;
    pdfSettings.overlay = false;
    pdfSettings.detectedFontScaling = 100/ 100.;
    return pdfSettings;
}

void TessOcr::ProcessPdf(const char *hocrtext, PageData pageData){
    QDomDocument doc;
    doc.setContent(QString::fromUtf8(hocrtext));

    QDomElement pageDiv = doc.firstChildElement("div");
    QMap<QString, QString> attrs = HOCRItem::deserializeAttrGroup(pageDiv.attribute("title"));
    attrs["image"] = QString("'%1'").arg(pageData.filename);
    attrs["ppageno"] = QString::number(pageData.page);
    attrs["rot"] = QString::number(pageData.angle);
    attrs["res"] = QString::number(300);
    pageDiv.setAttribute("title", HOCRItem::serializeAttrGroup(attrs));
    m_hocrDocument.addPage(pageDiv, true);
}

void TessOcr::PrintChildren(PDFPainter& painter, const HOCRItem* item, const PDFSettings& pdfSettings, double imgScale) {
    if(!item->isEnabled()) {
        return;
    }
    QString itemClass = item->itemClass();
    QRect itemRect = item->bbox();
    int childCount = item->children().size();
    if(itemClass == "ocr_par" && pdfSettings.uniformizeLineSpacing) {
        double yInc = double(itemRect.height()) / childCount;
        double y = itemRect.top() + yInc;
        int baseline = childCount > 0 ? item->children()[0]->baseLine() : 0;
        for(int iLine = 0; iLine < childCount; ++iLine, y += yInc) {
            HOCRItem* lineItem = item->children()[iLine];
            int x = itemRect.x();
            int prevWordRight = itemRect.x();
            for(int iWord = 0, nWords = lineItem->children().size(); iWord < nWords; ++iWord) {
                HOCRItem* wordItem = lineItem->children()[iWord];
                if(!wordItem->isEnabled()) {
                    continue;
                }
                QRect wordRect = wordItem->bbox();
                if(pdfSettings.fontFamily.isEmpty()) {
                    painter.setFontFamily(wordItem->fontFamily(), wordItem->fontBold(), wordItem->fontItalic());
                }
                if(pdfSettings.fontSize == -1) {
                    painter.setFontSize(wordItem->fontSize() * pdfSettings.detectedFontScaling);
                }
                // If distance from previous word is large, keep the space
                if(wordRect.x() - prevWordRight > pdfSettings.preserveSpaceWidth * painter.getAverageCharWidth()) {
                    x = wordRect.x();
                }
                prevWordRight = wordRect.right();
                QString text = wordItem->text();
                painter.drawText(x, y + baseline, text);
                x += painter.getTextWidth(text + " ");
            }
        }
    } else if(itemClass == "ocr_line" && !pdfSettings.uniformizeLineSpacing) {
        int baseline = item->baseLine();
        double y = itemRect.bottom() + baseline;
        for(int iWord = 0, nWords = item->children().size(); iWord < nWords; ++iWord) {
            HOCRItem* wordItem = item->children()[iWord];
            QRect wordRect = wordItem->bbox();
            if(pdfSettings.fontFamily.isEmpty()) {
                painter.setFontFamily(wordItem->fontFamily(), wordItem->fontBold(), wordItem->fontItalic());
            }
            if(pdfSettings.fontSize == -1) {
                painter.setFontSize(wordItem->fontSize() * pdfSettings.detectedFontScaling);
            }
            painter.drawText(wordRect.x(), y, wordItem->text());
        }
    } else if(itemClass == "ocr_graphic" && !pdfSettings.overlay) {
        //QRect scaledItemRect(itemRect.left() * imgScale, itemRect.top() * imgScale, itemRect.width() * imgScale, itemRect.height() * imgScale);
        QImage selection;
        //  QMetaObject::invokeMethod(this, "getSelection", QThread::currentThread() == qApp->thread() ? Qt::DirectConnection : Qt::BlockingQueuedConnection, Q_RETURN_ARG(QImage, selection), Q_ARG(QRect, scaledItemRect));
        painter.drawImage(itemRect, selection, pdfSettings);
    } else {
        for(int i = 0, n = item->children().size(); i < n; ++i) {
            PrintChildren(painter, item->children()[i], pdfSettings, imgScale);
        }
    }
}


int TessOcr::ExportPdf(const QString& outname, ProgressInfo *interProcessInfo)
{
    QPageSize pageSize = GetPdfPageSize(&m_hocrDocument);
    PDFSettings pdfSettings = GetPdfSettings();
    //init printer
    QPrinter printer;
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(outname);
    printer.setResolution(300);
    printer.setPageSize(pageSize);
    printer.setCreator("ZZOCR");
    printer.setFullPage(true);

    QFont defaultFont = QFont("Times");

    int pageCount = m_hocrDocument.pageCount();
    //QPainter can`t not used through processes
    QPainter painter(&printer);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPDFPainter pdfPrinter(&painter, defaultFont);
    if(!pdfSettings.fontFamily.isEmpty()) {
        pdfPrinter.setFontFamily(pdfSettings.fontFamily, false, false);
    }
    if(pdfSettings.fontSize != -1) {
        pdfPrinter.setFontSize(pdfSettings.fontSize);
    }

    for(int i = 0; i < pageCount; ++i) {
        const HOCRPage* page = m_hocrDocument.page(i);
        if(page->isEnabled()) {
            QRect bbox = page->bbox();
            int sourceDpi = page->resolution();
            int outputDpi = 300;
            double imgScale = double(outputDpi) / sourceDpi;
            PrintChildren(pdfPrinter, page, pdfSettings, imgScale);
            if(pdfSettings.overlay){
                QRect scaledBBox(imgScale * bbox.left(), imgScale * bbox.top(), imgScale * bbox.width(), imgScale * bbox.height());
                QImage selection;
                pdfPrinter.drawImage(bbox, selection, pdfSettings);
            }
            if( i != pageCount-1)
                printer.newPage();
        }
        interProcessInfo->m_progress+=(10.0*i/double(pageCount));
    }
    return ExportResult(outname, interProcessInfo);
}

int TessOcr::OcrPdf(const QString &infile, const PdfOcrParam &pdfOcrParam, ProgressInfo *interProcessInfo)
{
    QFileInfo fileInfo(infile);
    if(!fileInfo.exists()){
        interProcessInfo->m_errCode=1;
        return 1;
    }
    else if(!fileInfo.isFile()){
        interProcessInfo->m_errCode=2;
        return 2;
    }
    Poppler::Document* document=Poppler::Document::load(infile);;
    if(!document){
        interProcessInfo->m_errCode=3;//cant not load file
        return 3;
    }
    else if(document->isLocked() &&
            !document->unlock(pdfOcrParam.m_password.toLocal8Bit(), pdfOcrParam.m_password.toLocal8Bit())){
        interProcessInfo->m_errCode=4;//can not unlock
        return 4;
    }
    delete document;

    tesseract::TessBaseAPI tess;
    if(tess.Init(m_parentOfTessdataDir.toStdString().c_str(), pdfOcrParam.m_lang.toLatin1().data())==-1){
        interProcessInfo->m_errCode=5;
        return 5;
    }
    initRead(tess);
    ProgressMonitor monitor(pdfOcrParam.m_pages.size(), interProcessInfo);
    monitor.desc.ocr_alive =1;
    //add every pdf pages to document
    for(int page : pdfOcrParam.m_pages){
        monitor.desc.progress = 0;
        PageData pageData;
        pageData.success = false;
        pageData.filename = infile;
        pageData.page = page;
        pageData.angle = 0;
        pageData.resolution= 300;
        pageData.ocrAreas = GetOCRAreas(fileInfo, pageData.resolution, pageData.page);
        for(const QImage &image : pageData.ocrAreas){
            tess.SetImage(image.bits(), image.width(), image.height(), 4, image.bytesPerLine());
            tess.SetSourceResolution(pageData.resolution);
            tess.Recognize(&monitor.desc);
            if(!monitor.Cancelled()){
                char *text=nullptr;
                if(m_outfileType == OUTFILE_TYPE::TXT){
                    //tess.SetVariable("hocr_font_info", "false");
                    text = tess.GetUTF8Text();
                    m_utf8Text.append(text);
                }
                else{
                    tess.SetVariable("hocr_font_info", "true");
                    text = tess.GetHOCRText(page);
                    ProcessPdf(text, pageData);
                }
                delete text;
            }
        }
        monitor.increaseProgress();
        if(monitor.Cancelled()) {
            break;
        }
    }
    if(monitor.Cancelled()== true)
    {
        interProcessInfo->m_errCode=6;
        return 6;
    }
    interProcessInfo->m_progress=100*0.9;
    return 0;
}

int TessOcr::ExporteXML(const QString& outname, ProgressInfo *interProcessInfo){
    std::ofstream ofs(outname.toStdString());
    ofs << m_hocrDocument.toHTML(1).toStdString();
    return ExportResult(outname, interProcessInfo);
}

int TessOcr::ExportTxt(const QString& outname, ProgressInfo *interProcessInfo){
    std::ofstream ofs(outname.toStdString());
    ofs<<m_utf8Text.toStdString();
    return ExportResult(outname, interProcessInfo);
}

int TessOcr::ExportResult(const QString& outname, ProgressInfo *interProgressInfo){
    if(QFileInfo(outname).exists()){
        interProgressInfo->m_errCode = 7;
    }
    interProgressInfo->m_progress=100;
    return QFileInfo(outname).exists()?0:7;
}
