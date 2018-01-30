#define  BOOST_DATE_TIME_NO_LIB 
#include <cstdlib>
#include <iostream>
#include <thread>
#include <string>
#include <fstream>
#ifdef WIN32
#include <windows.h>
#endif
#include "Config.h"
#include "Interprocess.hh"

#ifdef WIN32
std::wstring AnsiToUtf16(const std::string &ansiString)
{
    int nCharacters = MultiByteToWideChar(CP_ACP, MB_COMPOSITE, ansiString.c_str(), ansiString.length(), nullptr, 0);
    std::wstring utf16_str(nCharacters, '\0');
    MultiByteToWideChar(CP_ACP, MB_COMPOSITE, ansiString.c_str(), ansiString.length(), &utf16_str[0], nCharacters);
    return utf16_str;
}

std::string Utf16ToUtf8(const std::wstring &utf16)
{
    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(),
        utf16.length(), nullptr, 0,
        nullptr, nullptr);
    std::string utf8_str;
    utf8_str.resize(utf8_size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(),
        utf16.length(), &utf8_str[0], utf8_size,
        nullptr, nullptr);
    return utf8_str;
}
#endif

class TessWrapper{
public:
    TessWrapper():m_inPath(nullptr), m_outPath(nullptr),
        m_pageRange(nullptr), m_tessDataParentDir(nullptr), m_progressInfo(nullptr),
        m_tessLang(nullptr), m_pdfPostProcess(nullptr){}

    void InitInterProcessSpace(){
        shared_memory_object::remove(MEMORY_NAME);
        m_segment = managed_shared_memory(create_only, MEMORY_NAME, 65536);
        const ShmemAllocator alloc_inst(m_segment.get_segment_manager());
        m_inPath = m_segment.construct<MyString>(IN_PATH_NAME)(alloc_inst);
        m_outPath = m_segment.construct<MyString>(OUT_PATH_NAME)(alloc_inst);
        m_pageRange = m_segment.construct<PageRange>(PAGE_RANGE_NAME)(0, 0);
        m_tessDataParentDir = m_segment.construct<MyString>(TESS_DATA_NAME)(alloc_inst);
        m_progressInfo = m_segment.construct<ProgressInfo>(PROGRESS_INFO_NAME)(false, 0);
        m_tessLang = m_segment.construct<MyString>(TESS_LANG)(alloc_inst);
        m_pdfPostProcess = m_segment.construct<PdfPostProcess>(PDF_POST_PROCESS)(100, -1, true, 1);
    }
    void DestroyInterProcessSpace(){
        m_segment.destroy<MyString>(IN_PATH_NAME);
        m_segment.destroy<MyString>(OUT_PATH_NAME);
        m_segment.destroy<PageRange>(PAGE_RANGE_NAME);
        m_segment.destroy<MyString>(TESS_DATA_NAME);
        m_segment.destroy<ProgressInfo>(PROGRESS_INFO_NAME);
        m_segment.destroy<MyString>(TESS_LANG);
        m_segment.destroy<PdfPostProcess>(PDF_POST_PROCESS);
        shared_memory_object::remove(MEMORY_NAME);
    }
    void SetCommonData(string tessPath, string tessDataParentDir, string tessLang, const PdfPostProcess &pdfPostProcess){
        m_tessPath = tessPath;
        *m_tessDataParentDir = tessDataParentDir.c_str();
        *m_tessLang = tessLang.c_str();
        *m_pdfPostProcess=pdfPostProcess;
    }
    void SetTess(string inPath, string outPath, int start, int end){
        *m_inPath = inPath.c_str();
        *m_outPath = outPath.c_str();
        m_pageRange->first = start;
        m_pageRange->second = end;
    }
    virtual bool ResultCallback(string inPath, string outPath, const ProgressInfo *progressInfo){
        if (progressInfo->m_errCode==-1)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << "In:" << inPath<<", Out:" << outPath <<", Progress:"<<progressInfo->m_progress << std::endl;
        }
        return progressInfo->m_cancle;
    }
    int RunTess(){
        std::thread systemCmd([](string cmd){
            if (0 != std::system(cmd.c_str()))
                return 1;
            return 0;
        }, m_tessPath);
        systemCmd.detach();
        do
        {
            ResultCallback(m_inPath->c_str(), m_outPath->c_str(), m_progressInfo);
        } while (m_progressInfo->m_progress != 100 && !m_progressInfo->m_cancle && m_progressInfo->m_errCode == -1);
#ifdef DEBUG
        if(m_progressInfo->m_progress==100){
            std::cerr<<"OCR ended by success. OutPath is"<<m_outPath->c_str()<<std::endl;
        }
        else if(m_progressInfo->m_cancle){
            std::cerr<<"OCR ended by cancled by User"<<std::endl;
        }
        else{
            std::cerr<< "OCR ended by error code which is "<<m_progressInfo->m_errCode<<std::endl;
        }
#endif
        return 0;
    }
    void StopTess(){
        if (m_progressInfo!=nullptr)
        {
            m_progressInfo->m_cancle = true;
        }
    }
    ~TessWrapper(){
        DestroyInterProcessSpace();
    }
private:
    managed_shared_memory m_segment;
    MyString *m_inPath;
    MyString *m_outPath;
    PageRange *m_pageRange;
    MyString *m_tessDataParentDir;
    ProgressInfo *m_progressInfo;
    MyString *m_tessLang;
    PdfPostProcess *m_pdfPostProcess;
    string m_tessPath;
};

int RunTess(string inPath, string outPath, int start, int end,
            string tessPath, string tessDataDir, string tessLang, const PdfPostProcess &pdfPostProcess){
    TessWrapper tessWrapper;
    tessWrapper.InitInterProcessSpace();
    tessWrapper.SetCommonData(tessPath, tessDataDir, tessLang, pdfPostProcess);
    tessWrapper.SetTess(inPath, outPath, start, end);
    return tessWrapper.RunTess();
}

int main(int argc, char *argv[])
{

    if(argc==1){
        std::cout<<"Usage: FrontUI inPath outPath start end config"<<std::endl;
        std::cout<<"outPath ext:pdf,txt,xml"<<std::endl;
        return 0;
    }

    //config
    auto configPath = argv[5];
    std::ifstream ifs(configPath);
    if(!ifs){
        std::cerr <<"Unable to open config file. your config path:"<<configPath<<std::endl;
        return 2;
    }
#ifdef WIN32
    string inPath = Utf16ToUtf8(AnsiToUtf16(argv[1])).c_str();
    string outPath = Utf16ToUtf8(AnsiToUtf16(argv[2])).c_str();
#else
    string inPath = argv[1];
    string outPath = argv[2];
#endif
    auto start = std::stoi(argv[3]);
    auto end = std::stoi(argv[4]);
    std::vector<std::string> config;
    config.resize(7);
    int i=0;
    while (i<7 && std::getline(ifs, config[i++], ' ')) {}
    auto tessPath = config[0];
    auto tessDataDir = config[1];
    auto tessLang = config[2];
    auto fontScale = std::stoi(config[3]);
    auto fontSize = std::stoi(config[4]);
    auto uniformziLineSpacing =bool(std::stoi(config[5]));
    auto preserveSpaceWidth=std::stoi(config[6]);
    PdfPostProcess pdfPostProcess(fontScale, fontSize, uniformziLineSpacing, preserveSpaceWidth);
    return RunTess(inPath, outPath, start, end, tessPath.c_str(), tessDataDir.c_str(), tessLang.c_str(), pdfPostProcess);
}
