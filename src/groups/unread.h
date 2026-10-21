// Reads "you have unread items" signals that apps put in their window titles, e.g.
//   "(3) Discord", "[12] …", "Inbox (5) - me@x.com - Gmail", "받은편지함 (7)", "4 new messages",
//   Slack's "! …" (mention) / "* …" (unread) prefixes.
#pragma once

#include <string>

namespace wp {

struct UnreadInfo {
    int count = 0;        // 0 when the title carries no number
    bool marker = false;  // unread/mention marker without a number (Slack-style "!" / "*")
};

UnreadInfo ParseUnread(const std::wstring& title);

}  // namespace wp
