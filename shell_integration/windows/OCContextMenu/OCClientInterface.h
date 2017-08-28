/**
* Copyright (c) 2015 ownCloud GmbH. All rights reserved.
*
* This library is free software; you can redistribute it and/or modify it under
* the terms of the GNU Lesser General Public License as published by the Free
* Software Foundation; version 2.1 of the License
*
* This library is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
* FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
* details.
*/

/**
* Copyright (c) 2014 ownCloud GmbH. All rights reserved.
*
* This library is free software; you can redistribute it and/or modify it under
* the terms of the GNU Lesser General Public License as published by the Free
* Software Foundation; version 2.1 of the License
*
* This library is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
* FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
* details.
*/

#ifndef AbstractSocketHandler_H
#define AbstractSocketHandler_H

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

class CommunicationSocket;

class OCClientInterface
{
public:
    struct ContextMenuInfo {
        std::vector<std::wstring> watchedDirectories;
        std::wstring contextMenuTitle;
        std::wstring shareMenuTitle;
        std::wstring copyLinkMenuTitle;
        std::wstring emailLinkMenuTitle;
    };
    static ContextMenuInfo FetchInfo();

    static void RequestShare(const std::wstring &path);
    static void RequestCopyLink(const std::wstring &path);
    static void RequestEmailLink(const std::wstring &path);

private:
    static void SendRequest(wchar_t *verb, const std::wstring &path);
};

#endif //ABSTRACTSOCKETHANDLER_H
