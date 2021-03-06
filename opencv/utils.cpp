#include "utils.hpp"

extern "C" {
    #include <libavutil/error.h>
}

#define ERR_STRING_BUF_SIZE 50

char err_string[ERR_STRING_BUF_SIZE];

char* errString(int errnum) {
    return av_make_error_string(err_string, ERR_STRING_BUF_SIZE, errnum);
}
