#include "Tessocr.hh"
#include <QApplication>
#include <iostream>
#include "Config.h"

int main(int argc, char *argv[])
{
    QApplication   app(argc, argv);
    //Open the managed segment
    managed_shared_memory segment(open_only, MEMORY_NAME);
    MyString *inPath = segment.find<MyString>(IN_PATH_NAME).first;
    MyString *outPath = segment.find<MyString>(OUT_PATH_NAME).first;
    PageRange *pageRange = segment.find<PageRange>(PAGE_RANGE_NAME).first;
    MyString *tessDataParentDir= segment.find<MyString>(TESS_DATA_NAME).first;
    MyString *tessLang = segment.find<MyString>(TESS_LANG).first;
    ProgressInfo *interProgressInfo = segment.find<ProgressInfo>(PROGRESS_INFO_NAME).first;
    PdfPostProcess *pdfPostProcess = segment.find<PdfPostProcess>(PDF_POST_PROCESS).first;


    if(inPath == nullptr || outPath==nullptr || pageRange==nullptr || tessDataParentDir==nullptr || interProgressInfo==nullptr){
        std::cerr << "Fail to open share memory."<<std::endl;
        return ERROR_CODE::FAIL_FIND_SHARE_MEMORY;
    }

    QList<int> pageRangeLst;
    for(int index = pageRange->first; index<=pageRange->second; index++){
        pageRangeLst.push_back(index);
    }
    OcrParam ocrParam("",tessLang->c_str(), pageRangeLst, *pdfPostProcess);
    TessOcr tessOcr(tessDataParentDir->data());

    QString outPathSuffix = QFileInfo(outPath->c_str()).suffix().toLower();
    QString inPathSuffix = QFileInfo(inPath->c_str()).suffix().toLower();

    tessOcr.SetInfileType(inPathSuffix=="pdf"?TessOcr::PDF:(inPathSuffix=="xml"?TessOcr::XML:TessOcr::IMG));
    tessOcr.SetOutfileType(outPathSuffix=="pdf"?TessOcr::PDF:(outPathSuffix=="txt"?TessOcr::TXT : TessOcr::XML));
    ERROR_CODE result;
    switch (tessOcr.GetInfileType()) {
    case TessOcr::PDF:
    case TessOcr::IMG:
        result = tessOcr.Ocr(inPath->c_str(), ocrParam, interProgressInfo);
        break;
    default:
        result = tessOcr.ParseXML(inPath->c_str(), interProgressInfo);
        break;
    }


    if(result==ERROR_CODE::SUCCESS){
        switch (tessOcr.GetOutfileType()) {
        case TessOcr::PDF:
            return tessOcr.ExportPdf(outPath->c_str(), interProgressInfo);
        case TessOcr::XML:
            return tessOcr.ExporteXML(outPath->c_str(), interProgressInfo);
        default:
            return tessOcr.ExportTxt(outPath->c_str(), interProgressInfo);
        }
    }
    else{
        return result;
    }

}
