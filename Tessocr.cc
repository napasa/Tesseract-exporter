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

TessOcr::TessOcr(const QString &parentOfTessdataDir)
    :m_parentOfTessdataDir(parentOfTessdataDir){
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

PDFSettings &TessOcr::GetPdfSettings(){
    return m_pdfSettings;
}

int sourceDpi=300;
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
                    double realPointSize = double(72)*wordRect.height()/sourceDpi;
#ifdef DEBUG
                    if(realPointSize>100){
                        std::cerr<<"ParseXML Error"<<std::endl;
                    }
#endif
                    painter.setFontSize(realPointSize * pdfSettings.detectedFontScaling);
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
                double realPointSize = double(72)*wordRect.height()/sourceDpi;
                painter.setFontSize(realPointSize * pdfSettings.detectedFontScaling);
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


ERROR_CODE TessOcr::ExportPdf(const QString& outPath, ProgressInfo *interProcessInfo)
{
    QPageSize pageSize = GetPdfPageSize(&m_hocrDocument);
    PDFSettings pdfSettings = GetPdfSettings();
    //init printer
    QPrinter printer;
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(outPath);
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
            sourceDpi = page->resolution();
            if(sourceDpi == 0){
                sourceDpi = 300;
            }
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
        interProcessInfo->m_progress+=(10.0*(i+1)/double(pageCount));
    }
    return ExportResult(outPath, interProcessInfo);
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
    //auto r = pageDiv.nextSiblingElement("div").isNull();
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
    ofs << m_hocrDocument.toHTML(1).toStdString();
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

ERROR_CODE TessOcr::Ocr(const QString &inPath, const OcrParam &pdfOcrParam, ProgressInfo *interProcessInfo){

    QFileInfo fileInfo(inPath);
    ERROR_CODE fileStatus = CheckFileStatus(fileInfo, interProcessInfo);
    if(fileStatus !=ERROR_CODE::SUCCESS)
        return fileStatus;

    double resolution;
    int angle =0;
    int progress =0;
    if(!inPath.compare("pdf", Qt::CaseInsensitive)){
        resolution = 100;
    }
    else{
        resolution = 300;
    }

    //font setting
    m_pdfSettings.detectedFontScaling = pdfOcrParam.m_pdfPostProcess.m_fontScale/100.0;
    m_pdfSettings.fontSize = pdfOcrParam.m_pdfPostProcess.m_fontSize;//defalut:-1;
    m_pdfSettings.uniformizeLineSpacing = pdfOcrParam.m_pdfPostProcess.m_uniformziLineSpacing;
    m_pdfSettings.preserveSpaceWidth = pdfOcrParam.m_pdfPostProcess.m_preserveSpaceWidth;

    tesseract::TessBaseAPI tess;
    if(tess.Init(m_parentOfTessdataDir.toStdString().c_str(), pdfOcrParam.m_lang.toLatin1().data())==-1){
        interProcessInfo->m_errCode=ERROR_CODE::FAIL_INIT_TESS;
        return ERROR_CODE::FAIL_INIT_TESS;
    }
    initRead(tess);
    ProgressMonitor monitor(pdfOcrParam.m_pages.size(), interProcessInfo);
    monitor.desc.ocr_alive =1;
    for(int page : pdfOcrParam.m_pages){
        monitor.desc.progress = progress;
        PageData pageData;
        pageData.success = false;
        pageData.filename = inPath;
        pageData.page = page;
        pageData.angle = angle;
        pageData.resolution=resolution;
        pageData.ocrAreas = GetOCRAreas(fileInfo, pageData.resolution, pageData.page);
        //add evert pdf pages to document
        for(const QImage &image : pageData.ocrAreas){
            tess.SetImage(image.bits(), image.width(), image.height(), 4, image.bytesPerLine());
            tess.SetSourceResolution(pageData.resolution);
            tess.Recognize(&monitor.desc);
            if(!monitor.Cancelled()){
                char *text=nullptr;
                if(m_outfileType == FILE_TYPE::TXT){
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
        interProcessInfo->m_errCode=ERROR_CODE::CANCLED_BY_USER;
        return ERROR_CODE::CANCLED_BY_USER;
    }
    interProcessInfo->m_progress=100*0.9;
    return ERROR_CODE::SUCCESS;
}
