
#include "../common/global_define.h"
#include "../common/eqemu_logsys.h"
#include <string.h>
#include "../common/string_util.h"
#include <curl/curl.h>
#include "slack.h"
const char *Slack::RAID_MOB_INFO = "#raid_mob_info";
const char *Slack::CSR = "#csr";
const char *Slack::OPS = "#ops";
void Slack::SendMessageTo(const char* channel, const char* message)
{
    CURL* curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    std::string payload = StringFormat("payload={\"username\": \"Server Bot\", \"icon_emoji\":\":rotating_light:\", \"channel\": \"%s\", \"text\": \"%s\"}", channel, message);
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://hooks.slack.com/services/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");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        res = curl_easy_perform(curl);
        /* Check for errors */
        if(res != CURLE_OK) {
#if EQDEBUG >= 5
        Log.Out(Logs::General, Logs::None, "New petition webhook failed");
#endif
        }
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}

void Slack::SendError(const char* message)
{
    CURL* curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    std::string payload = StringFormat("error=\"%s\"", message);
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "http://p2002.com/errors/index.php");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        res = curl_easy_perform(curl);
        /* Check for errors */
        if(res != CURLE_OK) {
#if EQDEBUG >= 5
        Log.Out(Logs::General, Logs::None, "New quest error webhook failed");
#endif
        }
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}
