/*
 * Copyright 2011-2013 Esrille Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "HTTPRequest.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include "utf.h"

#include "url/URI.h"
#include "http/HTTPCache.h"
#include "http/HTTPConnection.h"

#include "css/Box.h"
#include "css/BoxImage.h"

namespace org { namespace w3c { namespace dom { namespace bootstrap {

const unsigned short HttpRequest::UNSENT;
const unsigned short HttpRequest::OPENED;
const unsigned short HttpRequest::HEADERS_RECEIVED;
const unsigned short HttpRequest::LOADING;
const unsigned short HttpRequest::COMPLETE;
const unsigned short HttpRequest::DONE;

std::string HttpRequest::aboutPath;
std::string HttpRequest::cachePath("/tmp");

int HttpRequest::getContentDescriptor()
{
    if (filePath.empty())
        return -1;
    return ::open(filePath.c_str(), O_RDONLY, 0);
}

std::FILE* HttpRequest::openFile()
{
    if (filePath.empty())
        return 0;
    return fopen(filePath.c_str(), "rb");
}

std::fstream& HttpRequest::getContent()
{
    if (content.is_open())
        return content;

    char tempPath[PATH_MAX];
    if (PATH_MAX <= cachePath.length() + 9)
        return content;
    strcpy(tempPath, cachePath.c_str());
    strcat(tempPath, "/esrille-XXXXXX");
    int fd = mkstemp(tempPath);
    if (fd == -1)
        return content;
    filePath = tempPath;
    content.open(tempPath, std::ios_base::trunc | std::ios_base::in | std::ios_base::out | std::ios::binary);
    close(fd);
    return content;
}

void HttpRequest::setHandler(boost::function<void (void)> f)
{
    handler = f;
}

void HttpRequest::clearHandler()
{
    handler.clear();
    for (auto i = callbackList.begin(); i != callbackList.end(); ++i) {
        if (*i) {
            (*i)();
            *i = 0;
        }
    }
}

unsigned HttpRequest::addCallback(boost::function<void (void)> f, unsigned id)
{
    if (f) {
        if (readyState == DONE) {
            f();
            return static_cast<unsigned>(-1);
        }
        if (id < callbackList.size()) {
            callbackList[id] = f;
            return id;
        }
        callbackList.push_back(f);
        return callbackList.size() - 1;
    }
    return static_cast<unsigned>(-1);
}

void HttpRequest::clearCallback(unsigned id)
{
    if (id < callbackList.size())
        callbackList[id] = 0;
}

bool HttpRequest::redirect(const HttpResponseMessage& res)
{
    int method = request.getMethodCode();
    if (method != HttpRequestMessage::GET && method != HttpRequestMessage::HEAD)
        return false;
    if (!res.shouldRedirect())
        return false;
    std::string location = res.getResponseHeader("Location");
    if (!request.redirect(utfconv(location)))
        return false;

    // Redirect to location
    if (content.is_open())
        content.close();
    filePath.clear();
    cache = 0;
    readyState = OPENED;
    return true;
}

// Return true to put this request in the completed list.
bool HttpRequest::complete(bool error)
{
    errorFlag = error;
    if (!error)
        response.getLastModifiedValue(lastModified);
    else
        response.setStatus(404);
    readyState = (cache || handler || !callbackList.empty()) ? COMPLETE : DONE;
    return readyState == COMPLETE;
}

void HttpRequest::notify()
{
    if (cache)
        cache->notify(this, errorFlag);
    if (!errorFlag && redirect(response)) {
        response.clear();
        send();
        return;
    }

    if (handler) {
        handler();
        handler.clear();
    }
    for (auto i = callbackList.begin(); i != callbackList.end(); ++i) {
        if (*i) {
            (*i)();
            *i = 0;
        }
    }

    readyState = DONE;
}

bool HttpRequest::notify(bool error)
{
    if (complete(error))
        notify();
    return errorFlag;
}

void HttpRequest::open(const std::u16string& method, const std::u16string& urlString)
{
    URL url(base, urlString);
    request.open(utfconv(method), url);
    readyState = OPENED;
}

void HttpRequest::setRequestHeader(const std::u16string& header, const std::u16string& value)
{
    request.setHeader(utfconv(header), utfconv(value));
}

bool HttpRequest::constructResponseFromCache(bool sync)
{
    assert(cache);
    readyState = COMPLETE;
    errorFlag = false;

    response.update(cache->getResponseMessage());
    response.updateStatus(cache->getResponseMessage());
    response.getLastModifiedValue(lastModified);

    // TODO: deal with partial...
    filePath = cache->getFilePath();

    cache = 0;
    if (sync)
        notify();
    else
        HttpConnectionManager::getInstance().complete(this, errorFlag);

    readyState = DONE;

    return errorFlag;
}

namespace {

bool decodeBase64(std::fstream& content, const std::string& data)
{
    static const char* const table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char buf[4];
    char out[3];
    const char* p = data.c_str();
    int i = 0;
    int count = 3;
    while (char c = *p++) {
        if (c == '=') {
            buf[i++] = 0;
            if (--count <= 0)
                return false;
        } else if (const char* found = strchr(table, c))
            buf[i++] = found - table;
        else if (isspace(c))
            continue;
        else
            return false;
        if (i == 4) {
            out[0] = ((buf[0] << 2) & 0xfc) | ((buf[1] >> 4) & 0x03);
            out[1] = ((buf[1] << 4) & 0xf0) | ((buf[2] >> 2) & 0x0f);
            out[2] = ((buf[2] << 6) & 0xc0) | (buf[3] & 0x3f);
            content.write(out, count);
            i = 0;
            count = 3;
        }
    }
    return i == 0;
}

}  // namespace

bool HttpRequest::constructResponseFromData()
{
    URI dataURI(request.getURL());
    const std::string& data(dataURI);
    size_t end = data.find(',');
    if (end == std::string::npos) {
        notify(true);
        return errorFlag;
    }
    bool base64(false);
    if (7 <= end && data.compare(end - 7, 7, ";base64") == 0) {
        end -= 7;
        base64 = true;
    }
    response.parseMediaType(data.c_str() + 5, data.c_str() + end);
    std::fstream& content = getContent();
    if (!content.is_open()) {
        notify(true);
        return errorFlag;
    }
    flags &= ~DONT_REMOVE;
    if (!base64) {
        end += 1;
        std::string decoded(URI::percentDecode(URI::percentDecode(data, end, data.length() - end)));
        content << decoded;
    } else {
        end += 8;
        std::string decoded(URI::percentDecode(URI::percentDecode(data, end, data.length() - end)));
        errorFlag = !decodeBase64(content, decoded);
    }
    content.flush();
    notify(errorFlag);
    return errorFlag;
}

bool HttpRequest::send()
{
    if (3 <= getLogLevel())
        std::cerr << "HttpRequest::send(): " << request.getURL() << '\n';

    if (request.getURL().isEmpty())
        return notify(false);

    flags |= DONT_REMOVE;

    if (request.getURL().testProtocol(u"file")) {
        if (request.getMethodCode() != HttpRequestMessage::GET)
            return notify(true);
        std::u16string host = request.getURL().getHostname();
        if (!host.empty() && host != u"localhost")  // TODO: maybe allow local host IP addresses?
            return notify(true);
        filePath = utfconv(request.getURL().getPathname());
        struct stat status;
        if (stat(filePath.c_str(), &status) == 0 && S_ISREG(status.st_mode)) {
            lastModified = status.st_mtime;
            return notify(false);
        }
        return notify(true);
    }

    if (request.getURL().testProtocol(u"about")) {
        if (aboutPath.empty() || request.getMethodCode() != HttpRequestMessage::GET)
            return notify(true);
        filePath = utfconv(request.getURL().getPathname());
        if (filePath.empty())
            filePath = aboutPath + "/about/index.html";
        else
            filePath = aboutPath + "/about/" + filePath;
        struct stat status;
        if (stat(filePath.c_str(), &status) == 0 && S_ISREG(status.st_mode)) {
            lastModified = status.st_mtime;
            return notify(false);
        }
        return notify(true);
    }

    if (request.getURL().testProtocol(u"data"))
        return constructResponseFromData();

    cache = HttpCacheManager::getInstance().send(this);
    if (!cache || cache->isBusy())
        return false;
    return constructResponseFromCache(true);
}

void HttpRequest::abort()
{
    if (readyState == UNSENT)
        return;

    // TODO: implement more details.
    clearHandler();
    HttpConnectionManager& manager = HttpConnectionManager::getInstance();
    manager.abort(this);
    readyState = UNSENT;
    errorFlag = false;
    request.clear();
    response.clear();
    if (content.is_open())
        content.close();
    filePath.clear();   // TODO: Check if we should remove file now
    cache = 0;
}

void HttpRequest::cancel()
{
    // TODO: if the request has been sent but still remains in the request
    // queue of the HTTP connection, it is better to remove the request from
    // the queue.

    flags |= CANCELED;
    switch (readyState) {
    case UNSENT:
    case DONE:
        delete this;
        break;
    default:
        break;
    }
}

unsigned short HttpRequest::getStatus() const
{
    return response.getStatus();
}

const std::string& HttpRequest::getStatusText() const
{
    return response.getStatusText();
}

const std::string HttpRequest::getResponseHeader(std::u16string header) const
{
    return response.getResponseHeader(utfconv(header));
}

std::string HttpRequest::getAllResponseHeaders() const
{
    return response.getAllResponseHeaders();
}

HttpRequest::HttpRequest(const std::u16string& base) :
    base(base),
    readyState(UNSENT),
    flags(DONT_REMOVE),
    errorFlag(false),
    cache(0),
    handler(0),
    lastModified(0),
    boxImage(0)
{
}

HttpRequest::~HttpRequest()
{
    if (!(flags & DONT_REMOVE))
        removeFile();
    abort();
    delete boxImage;
}

}}}}  // org::w3c::dom::bootstrap
