#include "include/cef_browser.h"
#include "include/cef_callback.h"
#include "include/cef_frame.h"
#include "include/cef_request.h"
#include "include/cef_resource_handler.h"
#include "include/cef_response.h"
#include "include/cef_scheme.h"
#include "include/wrapper/cef_helpers.h"

#include "include/cef_parser.h"

#include <filesystem>
#include <fstream>

#include <urlmon.h>

//#include "include/cef_version.h"

std::wstring MimeTypeFromString(const std::wstring& str) {
    LPWSTR pwzMimeOut = NULL;
    HRESULT hr = FindMimeFromData(NULL, str.c_str(), NULL, 0,
        NULL, FMFD_URLASFILENAME, &pwzMimeOut, 0x0);
    if (SUCCEEDED(hr)) {
        std::wstring strResult(pwzMimeOut);
        CoTaskMemFree(pwzMimeOut);
        return strResult;
    }
    return L"";
}

bool isFile(const std::filesystem::path& path) {
    DWORD attr = GetFileAttributesW(path.wstring().c_str());

    if (attr == INVALID_FILE_ATTRIBUTES)
        return false;

    return !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

class PenguSchemeHandler : public CefResourceHandler {
public:
    PenguSchemeHandler(const std::filesystem::path& root) { this->root = root; }

    bool ProcessRequest(CefRefPtr<CefRequest> request,
        CefRefPtr<CefCallback> callback) OVERRIDE {
        CEF_REQUIRE_IO_THREAD();

        // pengu://path/to/file
        std::wstring url = request->GetURL();
        CefURLParts parts;
        if (!CefParseURL(request->GetURL(), parts)) return false;
        std::wstring host = parts.host.str;
        std::wstring pathname = parts.path.str;
        std::wstringstream stream(host + pathname);
        std::wstring segment;

        auto path = root;

        while (std::getline(stream, segment, L'/')) path /= segment;

        mime = MimeTypeFromString(path.filename());

        printf("path: %ws\n", path.c_str());
        printf("mime: %ws\n", mime.c_str());

        if (isFile(path)) {
            input = std::ifstream(path, std::ios::binary);
            input.seekg(0, input.end);
            length = input.tellg();
            input.seekg(0, input.beg);

            callback->Continue();
            return true;
        }
        return false;
    }

    void GetResponseHeaders(CefRefPtr<CefResponse> response,
        int64& response_length,
        CefString& redirectUrl) OVERRIDE {
        CEF_REQUIRE_IO_THREAD();

        response->SetMimeType(mime);
        response->SetHeaderByName("Access-Control-Allow-Origin", "*", true);
        response->SetStatus(200);

        // Set the resulting response length.
        response_length = length;
    }

    void Cancel() OVERRIDE {
        CEF_REQUIRE_IO_THREAD();
        input.close();
    }

    bool ReadResponse(void* data_out,
        int bytes_to_read,
        int& bytes_read,
        CefRefPtr<CefCallback> callback) OVERRIDE {
        CEF_REQUIRE_IO_THREAD();

        bytes_read = 0;
        auto cursor = input.tellg();
        if (cursor == -1 || (size_t)cursor >= length) {
            input.close();
            return false;
        }

        auto left = length - cursor;
        auto read = left > bytes_to_read ? bytes_to_read : left;

        bytes_read = (int)read;
        input.read((char*)data_out, read);
        cursor = input.tellg();

        return true;
    }

private:
    std::ifstream input;

    std::wstring mime;

    size_t length = 0;

    std::filesystem::path root;

    IMPLEMENT_REFCOUNTING(PenguSchemeHandler);
    DISALLOW_COPY_AND_ASSIGN(PenguSchemeHandler);
};

class PenguSchemeHandlerFactory : public CefSchemeHandlerFactory {
public:
    PenguSchemeHandlerFactory(const std::filesystem::path& root) {
        this->root = root;
    }

    // Return a new scheme handler instance to handle the request.
    CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        const CefString& scheme_name,
        CefRefPtr<CefRequest> request) OVERRIDE {
        CEF_REQUIRE_IO_THREAD();
        return new PenguSchemeHandler(root);
    }

private:
    std::filesystem::path root;

    IMPLEMENT_REFCOUNTING(PenguSchemeHandlerFactory);
    DISALLOW_COPY_AND_ASSIGN(PenguSchemeHandlerFactory);
};

void RegisterPenguScheme(const std::filesystem::path& root) {
	CefRegisterSchemeHandlerFactory("pengu", "", new PenguSchemeHandlerFactory(root));
}