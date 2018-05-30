#include "Tessocr.hh"
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
#include <poppler-qt4.h>
#else
#include <poppler-qt5.h>
#endif
#include <fstream>
#include <thread>
#include <QTextStream>
#include <QImageReader>
#ifdef DEBUG
#include <iostream>
#endif
#include "HOCRDocument.hh"
#include "Render.hh"

OcrParam::OcrParam(const QString &password, const QString &lang,
                   const QList<int> &pages, const PdfPostProcess &pdfPostProcess)
    :m_password(password), m_lang(lang), m_pages(pages), m_pdfPostProcess(pdfPostProcess){
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

TessOcr::TessOcr(const QString &parentOfTessdataDir)
    :m_parentOfTessdataDir(parentOfTessdataDir){


    //complete tess data dir
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
    m_pdfSettings = pdfSettings;
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

    DisplayRenderer *render=nullptr;
    if(fileinfo.completeSuffix().toLower().compare("pdf")==0){
        render = new PDFRenderer(fileinfo.filePath(),"");
    }
    else{
        render = new ImageRenderer(fileinfo.filePath());
    }
    QImage image = render->render(page, resolution);
    QPixmap pixmap = QPixmap::fromImage(image);
    QRectF rect = GetSceneBoundingRect(pixmap);
    QImage processedImage = GetImage(rect, pixmap);
    delete render;
    return QList<QImage>() << processedImage;
}

QPageSize TessOcr::GetPdfPageSize(const HOCRDocument*hocrdocument){
    double pageWidth, pageHeight;
    const HOCRPage* page = hocrdocument->page(0);
    pageWidth = double(page->bbox().width())/page->resolution()*72;
    pageHeight = double(page->bbox().height())/page->resolution()*72;
    return QPageSize(QSize(pageWidth, pageHeight), "custom",QPageSize::ExactMatch);
}

void TessOcr::read(const char *hocrtext, PageData pageData){
    QDomDocument doc;
    doc.setContent(QString::fromUtf8(hocrtext));

    QDomElement pageDiv = doc.firstChildElement("div");
    QMap<QString, QString> attrs = HOCRItem::deserializeAttrGroup(pageDiv.attribute("title"));
    attrs["image"] = QString("'%1'").arg(pageData.filename);
    attrs["ppageno"] = QString::number(pageData.page);
    attrs["rot"] = QString::number(pageData.angle);
    attrs["res"] = QString::number(pageData.resolution);
    pageDiv.setAttribute("title", HOCRItem::serializeAttrGroup(attrs));
    m_hocrDocument.addPage(pageDiv, true);
}

PDFSettings &TessOcr::GetPdfSettings(){
    return m_pdfSettings;
}

void TessOcr::printChildren(PDFPainter& painter, const HOCRItem* item, const PDFSettings& pdfSettings, double px2pu, double imgScale){
    if(!item->isEnabled()) {
        return;
    }
    QString itemClass = item->itemClass();
    QRect itemRect = item->bbox();
    int childCount = item->children().size();
    if(itemClass == "ocr_par" && pdfSettings.uniformizeLineSpacing) {
        double yInc = double(itemRect.height()) / childCount;
        double y = itemRect.top() + yInc;
        QPair<double, double> baseline = childCount > 0 ? item->children()[0]->baseLine() : qMakePair(0.0, 0.0);
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
                //tesseract bug
                //painter.setFontFamily(pdfSettings.fontFamily.isEmpty() ? wordItem->fontFamily() : pdfSettings.fontFamily, wordItem->fontBold(), wordItem->fontItalic());
                painter.setFontFamily(pdfSettings.fontFamily.isEmpty() ? wordItem->fontFamily() : pdfSettings.fontFamily, false, false);
                if(pdfSettings.fontSize == -1) {
                    painter.setFontSize(wordItem->fontSize() * pdfSettings.detectedFontScaling);
                }
                // If distance from previous word is large, keep the space
                if(wordRect.x() - prevWordRight > pdfSettings.preserveSpaceWidth * painter.getAverageCharWidth() / px2pu) {
                    x = wordRect.x();
                }
                prevWordRight = wordRect.right();
                QString text = wordItem->text();
                double wordBaseline = (x - itemRect.x()) * baseline.first + baseline.second;
                painter.drawText(x * px2pu, (y + wordBaseline) * px2pu, text);
                x += painter.getTextWidth(text + " ") / px2pu;
            }
        }
    } else if(itemClass == "ocr_line" && !pdfSettings.uniformizeLineSpacing) {
        QPair<double, double> baseline = item->baseLine();
        for(int iWord = 0, nWords = item->children().size(); iWord < nWords; ++iWord) {
            HOCRItem* wordItem = item->children()[iWord];
            if(!wordItem->isEnabled()) {
                continue;
            }
            QRect wordRect = wordItem->bbox();
            //painter.setFontFamily(pdfSettings.fontFamily.isEmpty() ? wordItem->fontFamily() : pdfSettings.fontFamily, wordItem->fontBold(), wordItem->fontItalic());
            painter.setFontFamily(pdfSettings.fontFamily.isEmpty() ? wordItem->fontFamily() : pdfSettings.fontFamily, false, false);
            if(pdfSettings.fontSize == -1) {
                painter.setFontSize(wordItem->fontSize() * pdfSettings.detectedFontScaling);
            }
            double y = itemRect.bottom() + (wordRect.center().x() - itemRect.x()) * baseline.first + baseline.second;
            painter.drawText(wordRect.x() * px2pu, y * px2pu, wordItem->text());
        }
    } else if(itemClass == "ocr_graphic" && !pdfSettings.overlay) {
        /*QRect scaledItemRect(itemRect.left() * imgScale, itemRect.top() * imgScale, itemRect.width() * imgScale, itemRect.height() * imgScale);
        QRect printRect(itemRect.left() * px2pu, itemRect.top() * px2pu, itemRect.width() * px2pu, itemRect.height() * px2pu);
        QImage selection;
        QMetaObject::invokeMethod(this, "getSelection", QThread::currentThread() == qApp->thread() ? Qt::DirectConnection : Qt::BlockingQueuedConnection, Q_RETURN_ARG(QImage, selection), Q_ARG(QRect, scaledItemRect));
        painter.drawImage(printRect, selection, pdfSettings);*/
    } else {
        for(int i = 0, n = item->children().size(); i < n; ++i) {
            printChildren(painter, item->children()[i], pdfSettings, px2pu, imgScale);
        }
    }
}





ERROR_CODE TessOcr::ExportPdf(const QString& outPath, ProgressInfo *interProcessInfo)
{
    PDFPainter* painter = nullptr;
    int pageCount = m_hocrDocument.pageCount();
    QFont defaultFont = QFont("Source Han Sans TW");
    defaultFont.setPointSize(0);
    painter = new QPrinterPDFPainter(outPath, "转转OCR", defaultFont);
    PDFSettings pdfSettings = getPdfSettings();
    int outputDpi = 72;
    QString paperSize = "source";
    double pageWidth, pageHeight;
    QString errMsg;
    for(int i = 0; i < pageCount; ++i) {
        const HOCRPage* page = m_hocrDocument.page(i);
        if(page->isEnabled()){
            QRect bbox = page->bbox();
            int sourceDpi = page->resolution();
            double px2pt = (72.0 / sourceDpi);
            double imgScale = double(outputDpi) / sourceDpi;
            if(paperSize == "source") {
                pageWidth = bbox.width() * px2pt;
                pageHeight = bbox.height() * px2pt;
            }
            double offsetX = 0.5 * (pageWidth - bbox.width() * px2pt);
            double offsetY = 0.5 * (pageHeight - bbox.height() * px2pt);
            if(!painter->createPage(pageWidth, pageHeight, offsetX, offsetY, errMsg)) {
                return ERROR_CODE::FAIL_CREATE_PAGE;
            }
            printChildren(*painter, page, pdfSettings, px2pt, imgScale);
            painter->finishPage();
        }
    }
    painter->finishDocument(errMsg);
    return ERROR_CODE::SUCCESS;
}

ERROR_CODE TessOcr::ParseXML(const QString &inPath, ProgressInfo *interProcessInfo)
{
    QFile file(inPath);
    ERROR_CODE fileStatus = CheckFileStatus(file, interProcessInfo);
    if(fileStatus !=ERROR_CODE::SUCCESS)
        return fileStatus;
    if(!file.open(QIODevice::ReadOnly)){
        interProcessInfo->m_errCode = ERROR_CODE::NOT_EXIST_FILE;
        return ERROR_CODE::NOT_EXIST_FILE;
    }
    QTextStream ts(&file);
    QString xmlString=ts.readAll();
    file.close();

    QDomDocument doc;
    if(!doc.setContent(xmlString)){
        interProcessInfo->m_errCode=ERROR_CODE::FAIL_PARSE_XML;
        return ERROR_CODE::FAIL_PARSE_XML;
    }
    QDomElement pageDiv = doc.documentElement().firstChildElement("div");
    if(pageDiv.isNull()){
        interProcessInfo->m_errCode=ERROR_CODE::NO_PAGE;
        return ERROR_CODE::NO_PAGE;
    }

    while (!pageDiv.isNull()) {
        //save first object
        QString str;
        QTextStream stream(&str);
        QDomNode node = pageDiv;
        node.save(stream, QDomNode::CDATASectionNode /* = 4 */);
        //store document
        QDomDocument temp;
        temp.setContent(str);
        m_hocrDocument.addPage(temp.documentElement(), true);
        pageDiv = pageDiv.nextSiblingElement("div");
    }
    return ERROR_CODE::SUCCESS;
}

ERROR_CODE TessOcr::ExporteXML(const QString& outPath, ProgressInfo *interProcessInfo){
    std::ofstream ofs(outPath.toStdString());
    ofs << m_hocrDocument.toHTML().toStdString();
    return ExportResult(outPath, interProcessInfo);
}

ERROR_CODE TessOcr::ExportTxt(const QString& outPath, ProgressInfo *interProcessInfo){
    std::ofstream ofs(outPath.toStdString());
    ofs<<m_utf8Text.toStdString();
    return ExportResult(outPath, interProcessInfo);
}

ERROR_CODE TessOcr::ExportResult(const QString& outPath, ProgressInfo *interProgressInfo){
    interProgressInfo->m_progress=100;
    return QFileInfo(outPath).exists()?ERROR_CODE::SUCCESS:ERROR_CODE::NOT_EXIST_FILE;
}

ERROR_CODE TessOcr::CheckFileStatus(const QFileInfo &fileInfo, ProgressInfo *interProcessInfo, const OcrParam &pdfOcrParam){
    if(!fileInfo.exists()){
        interProcessInfo->m_errCode=ERROR_CODE::NOT_EXIST_FILE;
        return ERROR_CODE::NOT_EXIST_FILE;
    }
    else if(!fileInfo.isFile()){
        interProcessInfo->m_errCode=ERROR_CODE::NOT_FILE;
        return ERROR_CODE::NOT_FILE;
    }

    // extra checking for pdf
    if(fileInfo.completeSuffix().toLower().compare("pdf")){
        return ERROR_CODE::SUCCESS;
    }

    Poppler::Document* document=Poppler::Document::load(fileInfo.absoluteFilePath());;
    if(!document){
        interProcessInfo->m_errCode=ERROR_CODE::NOT_LOAD_FILE;//cant not load file
        return ERROR_CODE::NOT_LOAD_FILE;
    }
    else if(document->isLocked() &&
            !document->unlock(pdfOcrParam.m_password.toLocal8Bit(), pdfOcrParam.m_password.toLocal8Bit())){
        interProcessInfo->m_errCode=ERROR_CODE::LOCKED_FILE;//can not unlock
        return ERROR_CODE::LOCKED_FILE;
    }
    delete document;
    return ERROR_CODE::SUCCESS;
}

ERROR_CODE TessOcr::recognize(const QString &inPath, const OcrParam &pdfOcrParam, bool autodetectLayout,  ProgressInfo *interProcessInfo) {
    tesseract::TessBaseAPI tess;
    std::string tessdataDir = m_parentOfTessdataDir.toStdString();
    std::string lang = "chi_sim";

    tesseract::OcrEngineMode mode = tesseract::OcrEngineMode::OEM_LSTM_ONLY;
    if(tess.Init(tessdataDir.c_str(), lang.c_str(), mode)==-1){
        interProcessInfo->m_errCode=ERROR_CODE::FAIL_INIT_TESS;
        return ERROR_CODE::FAIL_INIT_TESS;
    }
    tess.SetPageSegMode(tesseract::PageSegMode::PSM_SINGLE_BLOCK);

    ProgressMonitor monitor(pdfOcrParam.m_pages.size(), interProcessInfo);
    monitor.desc.ocr_alive =1;
    for(int page : pdfOcrParam.m_pages){
        monitor.desc.progress = 0;
        PageData pageData = setPage(page, true, inPath);
        for(const QImage &image : pageData.ocrAreas){
            tess.SetImage(image.bits(), image.width(), image.height(), 4, image.bytesPerLine());
            tess.SetSourceResolution(pageData.resolution);
            tess.Recognize(&monitor.desc);
            if(!monitor.Cancelled()){
                char *text=nullptr;
                if(m_outfileType == FILE_TYPE::TXT){
                    text = tess.GetUTF8Text();
                    m_utf8Text.append(text);
                } else{
                    tess.SetVariable("hocr_font_info", "true");
                    text = tess.GetHOCRText(page);
                    read(text, pageData);
                }
                delete text;
            }

        }
        monitor.increaseProgress();
        if(monitor.Cancelled()){
            break;
        }
    }
    if(monitor.Cancelled()== true)
    {
        interProcessInfo->m_errCode=ERROR_CODE::CANCLED_BY_USER;
        return ERROR_CODE::CANCLED_BY_USER;
    }
    interProcessInfo->m_progress=100*0.9;
    return ERROR_CODE::SUCCESS;
}

PDFSettings TessOcr::getPdfSettings() const{
    PDFSettings pdfSettings;

    pdfSettings.colorFormat = QImage::Format::Format_Mono;
    pdfSettings.conversionFlags = Qt::ThresholdDither;
    pdfSettings.compression = PDFSettings::CompressJpeg ;
    pdfSettings.compressionQuality = 94;
    pdfSettings.fontFamily = "Source Han Sans TW";
    pdfSettings.fontSize = -1;
    pdfSettings.uniformizeLineSpacing = true;
    pdfSettings.preserveSpaceWidth = 1;
    pdfSettings.overlay = false;
    pdfSettings.detectedFontScaling = 75 / 100.;
    return pdfSettings;
}

PageData TessOcr::setPage(int page, bool autodetectLayout, QString filename){
    PageData pageData;
    pageData.success = true;
    pageData.filename = filename;
    pageData.angle = 0;
    pageData.resolution = filename.endsWith(".pdf", Qt::CaseInsensitive)?300:100;
    pageData.ocrAreas = GetOCRAreas(filename, pageData.resolution, page);
    pageData.page = page;
    return pageData;
}
