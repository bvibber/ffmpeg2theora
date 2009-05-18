#ifndef _F2T_AVINFO_H_
#define _F2T_AVINFO_H_

unsigned long long gen_oshash(char const *filename);
void json_format_info(FILE* output, AVFormatContext *ic, const char *url);

#endif
