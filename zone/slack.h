#ifndef SLACK_H
#define SLACK_H

#include "../common/global_define.h"
#include "../common/eqemu_logsys.h"
#include <string.h>
#include "../common/string_util.h"
#include <curl/curl.h>
class Slack;
class Slack
{
    public:
        Slack();
        ~Slack();
        static void SendMessageTo(const char* channel, const char* message);
};
#endif
