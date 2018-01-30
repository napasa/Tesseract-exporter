#include "Tessocr.hh"
#include <QApplication>
#include <iostream>
#include "Config.h"

int main(int argc, char *argv[])
{
    //Open the managed segment
    managed_shared_memory segment(open_only, MEMORY_NAME);
    MyString *intPath = segment.find<MyString>(IN_PATH_NAME).first;
    MyString *outPath = segment.find<MyString>(OUT_PATH_NAME).first;
    PageRange *pageRange = segment.find<PageRange>(PAGE_RANGE_NAME).first;
    MyString *tessDataParentDir= segment.find<MyString>(TESS_DATA_NAME).first;
    MyString *tessLang = segment.find<MyString>(TESS_LANG).first;
    ProgressInfo *interProgressInfo = segment.find<ProgressInfo>(PROGRESS_INFO_NAME).first;
    PdfPostProcess *pdfPostProcess = segment.find<PdfPostProcess>(PDF_POST_PROCESS).first;


    if(intPath == nullptr || outPath==nullptr || pageRange==nullptr || tessDataParentDir==nullptr || interProgressInfo==nullptr){
        std::cerr << "From TessOcr: pdfPath or pageRange is nullptr"<<std::endl;
        return -1;
    }

    QList<int> pageRangeLst;
    for(int index = pageRange->first; index<=pageRange->second; index++){
        pageRangeLst.push_back(index);
    }
    QApplication   app(argc, argv);
    PdfOcrParam pdfOcrParam("", tessLang->c_str(), pageRangeLst, *pdfPostProcess);
    TessOcr tessOcr(tessDataParentDir->data());
#ifdef DEBUG
    std::cerr <<"From TessOcr: filePath"<< intPath->c_str()<<" "<<outPath<<std::endl;
#endif
    QString suffix = QFileInfo(outPath->c_str()).suffix().toLower();
    tessOcr.SetOutfileType(suffix=="pdf"?TessOcr::PDF:(suffix=="txt"?TessOcr::TXT : TessOcr::XML));
    int result = tessOcr.OcrPdf(intPath->c_str(), pdfOcrParam, interProgressInfo);
    if(!result){
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
