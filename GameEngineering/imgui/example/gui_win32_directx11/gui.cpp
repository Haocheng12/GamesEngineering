

#define WIN32_LEAN_AND_MEAN 
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include<map>

#include <fmod.hpp>
#include <fmod_errors.h>
#pragma comment(lib, "fmod_vc.lib")

#include "ChatClient.h"

// Data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;



// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);




int main(int, char**)
{
    // Register window class
    WNDCLASSEXW wc = { sizeof(wc),CS_CLASSDC,WndProc,0L,0L,
        GetModuleHandle(nullptr),nullptr,nullptr,nullptr,nullptr,
        L"ImGui Chatroom",nullptr};
    ::RegisterClassExW(&wc);

    // Create application window (maximized)
    HWND hwnd = ::CreateWindowW(
        wc.lpszClassName,
        L"Chatroom",
        WS_OVERLAPPEDWINDOW | WS_MAXIMIZE,
        0, 0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // enable keyboard controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);


    static ChatClient chatClient;
    bool connected = chatClient.Connect("127.0.0.1", 65432);
    if (!connected) {
        // Handle error or exit
        std::cerr << "Could not connect to server.\n";
    }


    FMOD::System* fmodSystem = nullptr;
    FMOD::System_Create(&fmodSystem);
    fmodSystem->init(512, FMOD_INIT_NORMAL, nullptr);

    // Load 2 SFX 
    FMOD::Sound* sfxPublic = nullptr;
    FMOD::Sound* sfxPrivate = nullptr;

    fmodSystem->createSound("private.wav", FMOD_DEFAULT, nullptr, &sfxPublic);
    fmodSystem->createSound("public.wav", FMOD_DEFAULT, nullptr, &sfxPrivate);

   
    // Flags and data
    bool done = false;
    bool window_open = true;
 

    // small window to get the user's name
    bool show_name_popup = true;
    char user_name_buffer[64] = "";

    // Chat data
    char input_buffer[256] = "";
    std::vector<std::string> chat_messages;
    std::vector<std::string> connected_users;

    std::map<std::string, bool> openPrivateChat;
    std::map<std::string, std::vector<std::string>> privateChatMessages;


    // Main loop
    while (!done)
    {
        // Poll and handle messages
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        
        auto new_messages = chatClient.GetReceivedMessages();
        for (auto& m : new_messages)
        {
            // check private message
            std::size_t pos = m.find("(private):");
            if (pos != std::string::npos)
            {
                // parse the sender from the left side
                std::string sender = m.substr(0, pos);
                if (!sender.empty() && sender.back() == ' ')
                    sender.pop_back(); 

                // auto-open the private window
                openPrivateChat[sender] = true;

                // store the message in private chat
                privateChatMessages[sender].push_back(m);
                if (sfxPrivate)
                {
                    // play sound
                    fmodSystem->playSound(sfxPrivate, nullptr, false, nullptr);
                }
            }
            else
            {
                // normal chat
                chat_messages.push_back(m);
             
                if (sfxPublic)
                {
                    fmodSystem->playSound(sfxPublic, nullptr, false, nullptr);
                }
            }
        }


     
        auto new_user_list = chatClient.GetConnectedUsers();
        connected_users = new_user_list;
           
        // create small window to get user name
        if (show_name_popup)
        {
            ImGui::SetNextWindowSize(ImVec2(300, 120), ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2((ImGui::GetIO().DisplaySize.x - 300) * 0.5f,
                (ImGui::GetIO().DisplaySize.y - 120) * 0.5f),
                ImGuiCond_Always);

            ImGui::Begin("Enter Username",
                nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

            ImGui::Text("Please enter your user name:");
            bool pressedEnter = ImGui::InputText("##username", user_name_buffer, IM_ARRAYSIZE(user_name_buffer),
                ImGuiInputTextFlags_EnterReturnsTrue);

            // if click ok button
            if (ImGui::Button("OK", ImVec2(60, 0))||pressedEnter)
            {
                if (std::strlen(user_name_buffer) > 0)
                {
                    // Add to user list
                    chatClient.SendMessageToServer(user_name_buffer);
                }
                else
                {
                    // Set a default name if empty
                    chatClient.SendMessageToServer("UnnamedUser");
                }
                // Close the popup
                show_name_popup = false;
            }

            ImGui::End();
        }
        else
        {
            // Main window
            if (window_open)
            {
               
                ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_Once);

                
                if (ImGui::Begin("Chatroom", &window_open,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
                {
                    //  Top region with two column
                    ImGui::BeginChild("TopRegion", ImVec2(0, -60), false);
                    {
                        ImGui::Columns(2, "ChatColumns", false);

                        // left is User list
                        ImGui::BeginChild("UserList", ImVec2(0, 0), true);
                        {
                            ImGui::Text("Users");
                            ImGui::Separator();

                            for (auto& user : connected_users)
                            {
                                // user can not select themself
                                if (user == user_name_buffer)
                                {
                                 
                                    ImGui::Text("%s (You)", user.c_str());
                                }
                                else
                                {
                                    // Make it selectable 
                                    if (ImGui::Selectable(user.c_str(), false))
                                    {
                                        openPrivateChat[user] = true;
                                    }
                                }
                            }
                        }
                        ImGui::EndChild();


                        // right is public chat 
                        ImGui::NextColumn();
                        ImGui::BeginChild("ChatArea", ImVec2(0, 0), true);
                        {
                            ImGui::Text("Chat Messages");
                            ImGui::Separator();

                            for (auto& msg : chat_messages)
                            {
                                ImGui::TextWrapped("%s", msg.c_str());
                            }
                            
                            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                                ImGui::SetScrollHereY(1.0f);
                        }
                        ImGui::EndChild();

                        ImGui::Columns(1);
                    }
                    ImGui::EndChild();

                    // Bottom region 
                    ImGui::Separator();
                    ImGui::PushItemWidth(-60.0f);
                    // Create the text input with "Enter returns true"
                    bool pressedEnter = ImGui::InputText("##ChatInput",
                        input_buffer,
                        IM_ARRAYSIZE(input_buffer),
                        ImGuiInputTextFlags_EnterReturnsTrue);

                    // Same line for the Send button
                    ImGui::SameLine();
                    if (pressedEnter || ImGui::Button("Send", ImVec2(50, 0)))
                    {
                        // Only send if not empty
                        if (std::strlen(input_buffer) > 0)
                        {
                            // Send to the server
                            chatClient.SendMessageToServer(input_buffer);

                            // show locally with message user send themself:
                            chat_messages.push_back(std::string("ME: ")+ input_buffer);

                            // Clear the input buffer
                            input_buffer[0] = '\0';
                        }
                    }

                }
                ImGui::End(); // End main window
            }
        }
        //private chat windows
        for (auto& kv : openPrivateChat)
        {
            const std::string& user = kv.first;
            bool& windowOpen = kv.second; 

            if (windowOpen)
            {
               
                std::string title = "Private Chat with " + user;
                ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
                // If the user closes the window, we set windowOpen = false
                if (ImGui::Begin(title.c_str(), &windowOpen,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
                {
                    // Show messages in a scrolling region
                    ImGui::BeginChild(("PrivateScroll_" + user).c_str(), ImVec2(0, -40), true);
                    {
                        // Retrieve the message list for this user
                        auto& msgList = privateChatMessages[user];
                        for (auto& msg : msgList)
                        {
                            ImGui::TextWrapped("%s", msg.c_str());
                        }

                        // auto-scroll
                        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                            ImGui::SetScrollHereY(1.0f);
                    }
                    ImGui::EndChild();

                    // Input text for new messages
                    static char privateInput[256] = "";
                    ImGui::PushItemWidth(-80.0f);
                    bool enterPressed = ImGui::InputText(("##PrivateMsg_" + user).c_str(),
                        privateInput,
                        IM_ARRAYSIZE(privateInput),
                        ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (enterPressed || ImGui::Button("Send"))
                    {
                        if (std::strlen(privateInput) > 0)
                        {
                            // Add to local chat display
                            privateChatMessages[user].push_back("Me (private): " + std::string(privateInput));

                            
                            // Send private message to server in "PRIVATE|<target>|<message> format"
                           
                            std::string pm = "PRIVATE|" + user + "|" + privateInput;
                            chatClient.SendMessageToServer(pm);

                            // Clear the input
                            privateInput[0] = '\0';
                        }
                    }
                }
                ImGui::End();
            }
        }
        // Rendering
        ImGui::Render();
        const float clear_color[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    // Cleanup
    chatClient.Disconnect();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}


// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
