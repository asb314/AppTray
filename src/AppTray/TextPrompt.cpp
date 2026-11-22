#include "TextPrompt.h"

#include "Strings.h"
#include "Theme.h"
#include "UiCommon.h"

#include <commctrl.h>
#include <uxtheme.h>

namespace apptray
{
	namespace
	{
		constexpr wchar_t PromptWindowClass[] = L"AppTray.TextPrompt";
		constexpr int EditId = 301;

		// Layout at 96 DPI
		constexpr int BaseWidth = 380;
		constexpr int Padding = 14;
		constexpr int LabelHeight = 36; // two lines
		constexpr int EditHeight = 24;
		constexpr int ButtonWidth = 96;
		constexpr int ButtonHeight = 28;

		class PromptWindow
		{
		public:
			PromptWindow(HINSTANCE instance, HWND owner, std::wstring_view title, std::wstring_view label, std::wstring_view text)
				: instance_(instance), owner_(owner), title_(title), label_(label), text_(text)
			{
			}

			std::optional<std::wstring> Run()
			{
				Create();
				if (window_ == nullptr)
				{
					return std::nullopt;
				}
				const bool ownerEnabled = owner_ != nullptr && IsWindowEnabled(owner_);
				if (ownerEnabled)
				{
					EnableWindow(owner_, FALSE);
				}
				ShowWindow(window_, SW_SHOW);
				SetForegroundWindow(window_);
				SetFocus(edit_);
				SendMessageW(edit_, EM_SETSEL, 0, -1);

				MSG message{};
				while (!done_)
				{
					const BOOL got = GetMessageW(&message, nullptr, 0, 0);
					if (got <= 0)
					{
						if (got == 0)
						{
							PostQuitMessage(static_cast<int>(message.wParam)); // leave it for the main loop
						}
						break;
					}
					// Enter = OK, Esc = Cancel, Tab between controls
					if (!IsDialogMessageW(window_, &message))
					{
						TranslateMessage(&message);
						DispatchMessageW(&message);
					}
				}
				// The owner first: enabled before this window goes, it gets the activation back
				if (ownerEnabled)
				{
					EnableWindow(owner_, TRUE);
				}
				if (window_ != nullptr)
				{
					DestroyWindow(window_);
				}
				if (!accepted_)
				{
					return std::nullopt;
				}
				return std::move(text_);
			}

		private:
			int Scale(int value) const
			{
				return ScaleForDpi(value, dpi_);
			}

			void Create()
			{
				WNDCLASSEXW windowClass{};
				windowClass.cbSize = sizeof(windowClass);
				windowClass.lpfnWndProc = &PromptWindow::WindowProc;
				windowClass.hInstance = instance_;
				windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
				windowClass.lpszClassName = PromptWindowClass;
				RegisterClassExW(&windowClass); // fails harmlessly when already registered

				// Centered over the owner, else on the primary monitor's work area
				RECT around{};
				if (owner_ == nullptr || !GetWindowRect(owner_, &around))
				{
					SystemParametersInfoW(SPI_GETWORKAREA, 0, &around, 0);
				}
				dpi_ = GetDpiForRect(around);
				const int clientWidth = Scale(BaseWidth);
				const int clientHeight =
					Scale(Padding) * 3 + Scale(LabelHeight) + Scale(EditHeight) + Scale(Padding) / 2 + Scale(ButtonHeight);
				RECT frame{0, 0, clientWidth, clientHeight};
				const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
				const DWORD exStyle = WS_EX_DLGMODALFRAME;
				AdjustWindowRectExForDpi(&frame, style, FALSE, exStyle, dpi_);
				const int width = frame.right - frame.left;
				const int height = frame.bottom - frame.top;
				const int x = around.left + (around.right - around.left - width) / 2;
				const int y = around.top + (around.bottom - around.top - height) / 2;

				CreateWindowExW(
					exStyle, PromptWindowClass, title_.c_str(), style, x, y, width, height, owner_, nullptr, instance_, this);
				if (window_ == nullptr)
				{
					return;
				}
				theme_.Reload();
				theme_.ApplyToWindow(window_);
				font_ = CreateMessageFont(dpi_);

				const int padding = Scale(Padding);
				int top = padding;
				CreateChild(WC_STATICW, label_.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, padding, top,
					clientWidth - 2 * padding, Scale(LabelHeight));
				top += Scale(LabelHeight) + padding / 2;

				edit_ = CreateChild(WC_EDITW, text_.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
					EditId, padding, top, clientWidth - 2 * padding, Scale(EditHeight));
				top += Scale(EditHeight) + padding;

				const int buttonWidth = Scale(ButtonWidth);
				const int buttonHeight = Scale(ButtonHeight);
				const int right = clientWidth - padding;
				HWND cancel = CreateChild(WC_BUTTONW, text::CancelButton, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
					IDCANCEL, right - buttonWidth, top, buttonWidth, buttonHeight);
				HWND ok = CreateChild(WC_BUTTONW, text::OkButton, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK,
					right - 2 * buttonWidth - padding / 2, top, buttonWidth, buttonHeight);
				for (HWND button : {ok, cancel})
				{
					SetWindowTheme(button, theme_.IsDark() ? L"DarkMode_Explorer" : L"Explorer", nullptr);
				}
			}

			HWND CreateChild(const wchar_t* className, const wchar_t* text, DWORD style, int id, int x, int y, int width, int height)
			{
				HWND child = CreateWindowExW(
					0, className, text, style, x, y, width, height, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
					instance_, nullptr);
				SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_.get()), TRUE);
				return child;
			}

			void Finish(bool accept)
			{
				if (accept)
				{
					text_ = GetWindowTextString(edit_);
				}
				accepted_ = accept;
				done_ = true;
			}

			static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
			{
				PromptWindow* self = nullptr;
				if (message == WM_NCCREATE)
				{
					self = static_cast<PromptWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
					self->window_ = window;
					SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
				}
				else
				{
					self = reinterpret_cast<PromptWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
				}
				return self != nullptr ? self->HandleMessage(window, message, wParam, lParam)
									   : DefWindowProcW(window, message, wParam, lParam);
			}

			LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
			{
				switch (message)
				{
				case WM_ERASEBKGND:
				{
					RECT client{};
					GetClientRect(window, &client);
					FillRect(reinterpret_cast<HDC>(wParam), &client, theme_.WindowBrush());
					return 1;
				}
				case WM_CTLCOLORSTATIC:
				{
					HDC dc = reinterpret_cast<HDC>(wParam);
					SetTextColor(dc, theme_.Palette().text);
					SetBkColor(dc, theme_.Palette().window);
					return reinterpret_cast<LRESULT>(theme_.WindowBrush());
				}
				case WM_CTLCOLOREDIT:
				{
					HDC dc = reinterpret_cast<HDC>(wParam);
					SetTextColor(dc, theme_.Palette().text);
					SetBkColor(dc, theme_.Palette().surface);
					return reinterpret_cast<LRESULT>(theme_.SurfaceBrush());
				}
				case WM_COMMAND:
					if (LOWORD(wParam) == IDOK)
					{
						Finish(true);
					}
					else if (LOWORD(wParam) == IDCANCEL)
					{
						Finish(false);
					}
					return 0;
				case WM_CLOSE:
					Finish(false);
					return 0;
				case WM_DESTROY:
					// Also when the owner goes first (AppTray exits): that ends the prompt, cancelled
					window_ = nullptr;
					done_ = true;
					return 0;
				default:
					return DefWindowProcW(window, message, wParam, lParam);
				}
			}

			HINSTANCE instance_;
			HWND owner_;
			std::wstring title_;
			std::wstring label_;
			std::wstring text_;
			HWND window_ = nullptr;
			HWND edit_ = nullptr;
			UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
			Theme theme_;
			wil::unique_hfont font_;
			bool done_ = false;
			bool accepted_ = false;
		};
	}

	std::optional<std::wstring> ShowTextPrompt(
		HINSTANCE instance, HWND owner, std::wstring_view title, std::wstring_view label, std::wstring_view initialText)
	{
		PromptWindow prompt(instance, owner, title, label, initialText);
		return prompt.Run();
	}
}
