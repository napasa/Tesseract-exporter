#ifndef INTERPROCESS_HH
#define INTERPROCESS_HH
#define  BOOST_DATE_TIME_NO_LIB
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

using namespace boost::interprocess;
enum ERROR_CODE{
   PROCESSING_FILE=0,
   SUCCESS,
   UNIMPLEMENTED,
   NOT_EXIST_FILE,
   NOT_FILE,
   NOT_LOAD_FILE,
   LOCKED_FILE,
   FAIL_INIT_TESS,
   CANCLED_BY_USER,
   FAIL_OPEN_FILE,
   NO_PAGE,
   FAIL_PARSE_XML,
   FAIL_FIND_SHARE_MEMORY,
   CANT_NOT_GENERATE_IMAGE
};
struct ProgressInfo{
public:
    ProgressInfo(int progress)
        :m_progress(progress), m_errCode(ERROR_CODE::PROCESSING_FILE){}
    int m_progress;
    ERROR_CODE m_errCode;
};
//Define an STL compatible allocator of ints that allocates from the managed_shared_memory.
//This allocator will allow placing containers in the segment
typedef allocator<char, managed_shared_memory::segment_manager>  ShmemAllocator;
typedef basic_string<char, std::char_traits<char>, ShmemAllocator> MyString;
typedef std::pair<int, int> PageRange;
struct PdfPostProcess{
public:
    PdfPostProcess(){}
    PdfPostProcess(int fontScale, int fontSize, bool uniformziLineSpacing, int preserveSpaceWidth)
        : m_fontScale(fontScale), m_fontSize(fontSize), m_uniformziLineSpacing(uniformziLineSpacing)
    ,m_preserveSpaceWidth(preserveSpaceWidth){

    }
    int m_fontScale;/*0~100*/
    int m_fontSize;
    bool m_uniformziLineSpacing;
    int m_preserveSpaceWidth;
};
#endif // INTERPROCESS_HH
