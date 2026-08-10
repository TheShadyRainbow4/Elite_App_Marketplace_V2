#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <iostream>
#include "json.hpp"

using json = nlohmann::json;

class GeminiAI {
public:
    static std::string GetApiKey() {
        if (const char* env = getenv("GEMINI_API_KEY")) return std::string(env);
        char buf[2048] = {0};
        if (GetEnvironmentVariableA("GEMINI_API_KEY", buf, sizeof(buf))) return std::string(buf);
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD size = sizeof(buf);
            if (RegQueryValueExA(hKey, "GEMINI_API_KEY", NULL, NULL, (LPBYTE)buf, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey); return std::string(buf);
            }
            RegCloseKey(hKey);
        }
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD size = sizeof(buf);
            if (RegQueryValueExA(hKey, "GEMINI_API_KEY", NULL, NULL, (LPBYTE)buf, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey); return std::string(buf);
            }
            RegCloseKey(hKey);
        }
        return "";
    }

    static std::string GenerateContent(const std::string& prompt) {
        std::string apiKey = GetApiKey();
        if (apiKey.empty()) {
            return "ERROR: GEMINI_API_KEY environment variable not set.";
        }

        std::string host = "generativelanguage.googleapis.com";
        // Let's use gemini-2.5-flash as it's the standard for general fast text tasks.
        std::string path = "/v1beta/models/gemini-2.5-flash:generateContent?key=" + apiKey;

        HINTERNET hSession = WinHttpOpen(L"EliteManager/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return "ERROR: WinHttpOpen failed";

        std::wstring whost(host.begin(), host.end());
        HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return "ERROR: WinHttpConnect failed"; }

        std::wstring wpath(path.begin(), path.end());
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return "ERROR: WinHttpOpenRequest failed"; }

        json req;
        req["contents"] = json::array({{{"parts", json::array({{{"text", prompt}}})}}});
        
        // Disable safety settings as we don't want false positives when tagging apps
        req["safetySettings"] = json::array({
            {{"category", "HARM_CATEGORY_HARASSMENT"}, {"threshold", "BLOCK_NONE"}},
            {{"category", "HARM_CATEGORY_HATE_SPEECH"}, {"threshold", "BLOCK_NONE"}},
            {{"category", "HARM_CATEGORY_SEXUALLY_EXPLICIT"}, {"threshold", "BLOCK_NONE"}},
            {{"category", "HARM_CATEGORY_DANGEROUS_CONTENT"}, {"threshold", "BLOCK_NONE"}}
        });
        
        std::string payload = req.dump();

        std::wstring headers = L"Content-Type: application/json\r\n";

        bool bResults = WinHttpSendRequest(hRequest, headers.c_str(), -1, (LPVOID)payload.c_str(), payload.size(), payload.size(), 0);
        if (bResults) bResults = WinHttpReceiveResponse(hRequest, NULL);

        std::string responseData;
        if (bResults) {
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;
            do {
                dwSize = 0;
                WinHttpQueryDataAvailable(hRequest, &dwSize);
                if (dwSize == 0) break;
                char* pszOutBuffer = new char[dwSize + 1];
                ZeroMemory(pszOutBuffer, dwSize + 1);
                WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded);
                responseData += pszOutBuffer;
                delete[] pszOutBuffer;
            } while (dwSize > 0);
        } else {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "ERROR: HTTP Request failed";
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        try {
            json res = json::parse(responseData);
            if (res.contains("candidates") && res["candidates"].size() > 0) {
                return res["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
            } else {
                return "ERROR: No candidates in response: " + responseData;
            }
        } catch(...) {
            return "ERROR: JSON parse failed for response: " + responseData;
        }

        return "";
    }
};
