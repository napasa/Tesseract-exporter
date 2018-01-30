#ifndef INTERPROCESS_HH
#define INTERPROCESS_HH
#define  BOOST_DATE_TIME_NO_LIB
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

using namespace boost::interprocess;
struct ProgressInfo{
public:
    ProgressInfo(bool cancle, int progress)
        :m_cancle(cancle), m_progress(progress), m_errCode(-1){}
    bool m_cancle;
    int m_progress;
    int m_errCode;
};
//Define an STL compatible allocator of ints that allocates from the managed_shared_memory.
//This allocator will allow placing containers in the segment
typedef allocator<char, managed_shared_memory::segment_manager>  ShmemAllocator;
typedef basic_string<char, std::char_traits<char>, ShmemAllocator> MyString;
typedef std::pair<int, int> PageRange;
struct PdfPostProcess{
public:
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
