#include "StartupPrompt.h"

#include "Strings.h"
#include "Theme.h"
#include "UiCommon.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <format>

namespace apptray
{
	namespace
	{
		constexpr wchar_t PromptWindowClass[] = L"AppTray.StartupPrompt";
		constexpr UINT_PTR CountdownTimerId = 1;
		constexpr int ListId = 201;
		constexpr int CountdownId = 202;

		// Layout at 96 DPI
		constexpr int BaseWidth = 340;
		constexpr int Padding = 14;
		constexpr int RowHeight = 24;
		constexpr int ButtonWidth = 96;
		constexpr int ButtonHeight = 28;

		class PromptWindow
		{
		public:
			PromptWindow(HINSTANCE instance, std::vector<StartupChoice> choices, int countdownSeconds)
				: instance_(instance), choices_(std::move(choices)), remaining_(countdownSeconds)
			{
			}

			std::optional<std::vector<StartupChoice>> Run()
			{
				Create();
				if (window_ == nullptr)
				{
					return std::nullopt;
				}
				ShowWindow(window_, SW_SHOW);
				SetForegroundWindow(window_);
				SetFocus(list_);
				SetTimer(window_, CountdownTimerId, 1000, nullptr);

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
					// Enter = Abrir, Esc = Ninguna, Tab between controls, Space toggles a checkbox
					if (!IsDialogMessageW(window_, &message))
					{
						TranslateMessage(&message);
						DispatchMessageW(&message);
					}
				}
				if (window_ != nullptr)
				{
					DestroyWindow(window_);
				}
				if (!accepted_)
				{
					return std::nullopt;
				}
				return std::move(choices_);
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

				// Centered on the primary monitor's work area
				RECT work{};
				SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
				dpi_ = GetDpiForRect(work);
				const int rows = static_cast<int>(std::min<size_t>(choices_.size(), 8));
				const int clientWidth = Scale(BaseWidth);
				const int clientHeight =
					Scale(Padding) * 4 + Scale(20) + rows * Scale(RowHeight) + Scale(4) + Scale(20) + Scale(ButtonHeight);
				RECT frame{0, 0, clientWidth, clientHeight};
				const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
				const DWORD exStyle = WS_EX_TOPMOST | WS_EX_DLGMODALFRAME;
				AdjustWindowRectExForDpi(&frame, style, FALSE, exStyle, dpi_);
				const int width = frame.right - frame.left;
				const int height = frame.bottom - frame.top;
				const int x = work.left + (work.right - work.left - width) / 2;
				const int y = work.top + (work.bottom - work.top - height) / 2;

				CreateWindowExW(
					exStyle, PromptWindowClass, text::StartupPromptTitle, style, x, y, width, height, nullptr, nullptr,
					instance_, this);
				if (window_ == nullptr)
				{
					return;
				}
				theme_.Reload();
				theme_.ApplyToWindow(window_);
				font_ = CreateMessageFont(dpi_);

				const int padding = Scale(Padding);
				int top = padding;
				label_ = CreateChild(WC_STATICW, text::StartupPromptLabel, WS_CHILD | WS_VISIBLE | SS_LEFT, 0, padding, top,
					clientWidth - 2 * padding, Scale(20));
				top += Scale(20) + padding / 2;

				const int listHeight = rows * Scale(RowHeight) + Scale(4);
				list_ = CreateChild(
					WC_LISTVIEWW, L"",
					WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
					ListId, padding, top, clientWidth - 2 * padding, listHeight);
				ListView_SetExtendedListViewStyle(
					list_, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
				LVCOLUMNW column{};
				column.mask = LVCF_WIDTH;
				column.cx = clientWidth - 2 * padding - GetSystemMetricsForDpi(SM_CXVSCROLL, dpi_) - Scale(4);
				ListView_InsertColumn(list_, 0, &column);
				initializing_ = true;
				for (size_t i = 0; i < choices_.size(); ++i)
				{
					LVITEMW item{};
					item.mask = LVIF_TEXT;
					item.iItem = static_cast<int>(i);
					item.pszText = choices_[i].name.data();
					ListView_InsertItem(list_, &item);
					ListView_SetCheckState(list_, static_cast<int>(i), choices_[i].checked ? TRUE : FALSE);
				}
				if (!choices_.empty())
				{
					ListView_SetItemState(list_, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				}
				initializing_ = false;
				top += listHeight + padding / 2;

				countdown_ = CreateChild(WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_LEFT, CountdownId, padding, top,
					clientWidth - 2 * padding, Scale(20));
				UpdateCountdown();
				top += Scale(20) + padding / 2;

				const int buttonWidth = Scale(ButtonWidth);
				const int buttonHeight = Scale(ButtonHeight);
				const int right = clientWidth - padding;
				noneButton_ = CreateChild(WC_BUTTONW, text::NoneButton, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
					IDCANCEL, right - buttonWidth, top, buttonWidth, buttonHeight);
				openButton_ = CreateChild(WC_BUTTONW, text::OpenButton, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
					IDOK, right - 2 * buttonWidth - padding / 2, top, buttonWidth, buttonHeight);

				const ThemePalette& palette = theme_.Palette();
				theme_.ApplyToControl(list_);
				ListView_SetBkColor(list_, palette.surface);
				ListView_SetTextBkColor(list_, palette.surface);
				ListView_SetTextColor(list_, palette.text);
				for (HWND button : {openButton_, noneButton_})
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

			void UpdateCountdown()
			{
				const std::wstring text =
					counting_ ? std::format(L"{} {} s", text::CountdownPrefix, remaining_) : std::wstring(text::CountdownStopped);
				SetWindowTextW(countdown_, text.c_str());
			}

			void StopCountdown()
			{
				if (counting_)
				{
					counting_ = false;
					KillTimer(window_, CountdownTimerId);
					UpdateCountdown();
				}
			}

			void Finish(bool accept)
			{
				if (accept)
				{
					for (size_t i = 0; i < choices_.size(); ++i)
					{
						choices_[i].checked = ListView_GetCheckState(list_, static_cast<int>(i)) != FALSE;
					}
				}
				accepted_ = accept;
				done_ = true;
				KillTimer(window_, CountdownTimerId);
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
					const bool isCountdown = reinterpret_cast<HWND>(lParam) == countdown_;
					SetTextColor(dc, isCountdown ? theme_.Palette().secondaryText : theme_.Palette().text);
					SetBkColor(dc, theme_.Palette().window);
					return reinterpret_cast<LRESULT>(theme_.WindowBrush());
				}
				case WM_TIMER:
					if (wParam == CountdownTimerId && counting_)
					{
						if (--remaining_ <= 0)
						{
							Finish(true); // nobody touched it: go with the saved choice
						}
						else
						{
							UpdateCountdown();
						}
					}
					return 0;
				case WM_NOTIFY:
				{
					const auto* header = reinterpret_cast<const NMHDR*>(lParam);
					if (header->hwndFrom == list_ && header->code == LVN_ITEMCHANGED && !initializing_)
					{
						const auto* change = reinterpret_cast<const NMLISTVIEW*>(lParam);
						if ((change->uChanged & LVIF_STATE) != 0
							&& ((change->uNewState ^ change->uOldState) & LVIS_STATEIMAGEMASK) != 0)
						{
							StopCountdown(); // a checkbox was touched: wait for a button
						}
					}
					return 0;
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
					window_ = nullptr;
					return 0;
				default:
					return DefWindowProcW(window, message, wParam, lParam);
				}
			}

			HINSTANCE instance_;
			std::vector<StartupChoice> choices_;
			int remaining_;
			HWND window_ = nullptr;
			HWND label_ = nullptr;
			HWND list_ = nullptr;
			HWND countdown_ = nullptr;
			HWND openButton_ = nullptr;
			HWND noneButton_ = nullptr;
			UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
			Theme theme_;
			wil::unique_hfont font_;
			bool counting_ = true;
			bool initializing_ = false;
			bool done_ = false;
			bool accepted_ = false;
		};
	}

	std::optional<std::vector<StartupChoice>> ShowStartupPrompt(
		HINSTANCE instance, std::vector<StartupChoice> choices, int countdownSeconds)
	{
		if (choices.empty())
		{
			return std::vector<StartupChoice>{};
		}
		PromptWindow prompt(instance, std::move(choices), std::max(1, countdownSeconds));
		return prompt.Run();
	}
}
