#define  BOOST_DATE_TIME_NO_LIB 
#include <cstdlib>
#include <iostream>
#include <thread>
#include <string>
#include <fstream>
#include "Config.h"
#include "Interprocess.hh"

class TessWrapper{
public:
    TessWrapper():m_inPath(nullptr), m_outPath(nullptr),
        m_pageRange(nullptr), m_tessDataParentDir(nullptr), m_progressInfo(nullptr){}

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
    }
    void DestroyInterProcessSpace(){
        m_segment.destroy<MyString>(IN_PATH_NAME);
        m_segment.destroy<MyString>(OUT_PATH_NAME);
        m_segment.destroy<PageRange>(PAGE_RANGE_NAME);
        m_segment.destroy<MyString>(TESS_DATA_NAME);
        m_segment.destroy<ProgressInfo>(PROGRESS_INFO_NAME);
        m_segment.destroy<MyString>(TESS_LANG);
        shared_memory_object::remove(MEMORY_NAME);
    }
    void SetCommonData(string tessPath, string tessDataParentDir, string tessLang){
        m_tessPath = tessPath;
        *m_tessDataParentDir = tessDataParentDir.c_str();
        *m_tessLang = tessLang.c_str();
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
    string m_tessPath;
};

#define TESS_PATH "./EndProcess"
#define TESS_DATA "/home/yu/build-gImageReader-Imported_Kit-Debug/share/"
#define TESS_LANG "eng"

int RunTess(string inPath, string outPath, int start, int end, string tessPath, string tessDataDir, string tessLang){
    TessWrapper tessWrapper;
    tessWrapper.InitInterProcessSpace();
    tessWrapper.SetCommonData(tessPath, tessDataDir, tessLang);
    tessWrapper.SetTess(inPath, outPath, start, end);
    return tessWrapper.RunTess();
}

#ifdef WINDOWS
enum Encode
{
    ANSI = 1,
    UNICODE_LE,
    UNICODE_BE,
    UTF8,
    UTF8_NOBOM
};
bool CheckUnicodeWithoutBOM(const char *pText, long length)
{
    int i;
    int nBytes = 0;
    char chr;

    bool bAllAscii = true;
    for (i = 0; i < length; i++)
    {
        chr = *(pText + i);
        if ((chr & 0x80) != 0)
            bAllAscii = false;
        if (nBytes == 0)
        {
            if (chr >= 0x80)
            {
                if (chr >= 0xFC && chr <= 0xFD)
                    nBytes = 6;
                else if (chr >= 0xF8)
                    nBytes = 5;
                else if (chr >= 0xF0)
                    nBytes = 4;
                else if (chr >= 0xE0)
                    nBytes = 3;
                else if (chr >= 0xC0)
                    nBytes = 2;
                else
                {
                    return false;
                }
                nBytes--;
            }
        }
        else
        {
            if ((chr & 0xC0) != 0x80)
            {
                return false;
            }
            nBytes--;
        }
    }
    if (nBytes > 0)
    {
        return false;
    }
    if (bAllAscii)
    {
        return false;
    }
    return true;
}
Encode DetectEncode(const char *pBuffer, long length)
{
    if (length < 2)
    {
        return Encode::ANSI;
    }

    if (pBuffer[0] == 0xFF && pBuffer[1] == 0xFE)
    {
        return Encode::UNICODE_LE;
    }
    else if (pBuffer[0] == 0xFE && pBuffer[1] == 0xFF)
    {
        return Encode::UNICODE_BE;
    }
    else if (pBuffer[0] == 0xEF && pBuffer[1] == 0xBB && pBuffer[2] == 0xBF)
    {
        return Encode::UTF8;
    }
    else if (CheckUnicodeWithoutBOM(pBuffer, length))
    {
        return Encode::UTF8_NOBOM;
    }
    else
    {
        return Encode::ANSI;
    }
}
#endif

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
    auto inPath = argv[1];
    auto outPath = argv[2];
    auto start = std::stoi(argv[3]);
    auto end = std::stoi(argv[4]);
    std::vector<std::string> config;
    config.resize(3);
    int i=0;
    while (i<3 && std::getline(ifs, config[i++], ' ')) {}
    auto tessPath = config[0];
    auto tessDataDir = config[1];
    auto tessLang = config[2];
    return RunTess(inPath, outPath, start, end, tessPath.c_str(), tessDataDir.c_str(), tessLang.c_str());
}
