#include "ElevatedTask.h"

#include <windows.h>

#include <oleauto.h> // before wil: enables wil::unique_bstr
#include <taskschd.h>
#define SECURITY_WIN32
#include <security.h>

#include <wil/com.h>
#include <wil/resource.h>

#include <cstdint>
#include <cwctype>
#include <format>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "secur32.lib")

namespace fs = std::filesystem;

namespace apptray
{
	namespace
	{
		constexpr wchar_t TaskFolderName[] = L"AppTray";
		constexpr wchar_t TaskFolderPath[] = L"\\AppTray";

		wil::unique_bstr MakeBstr(std::wstring_view text)
		{
			return wil::unique_bstr(SysAllocStringLen(text.data(), static_cast<UINT>(text.size())));
		}

		VARIANT MakeEmptyVariant()
		{
			VARIANT value;
			VariantInit(&value);
			return value;
		}

		std::wstring DescribeHresult(std::wstring_view action, HRESULT result)
		{
			return std::format(L"{} (0x{:08X})", action, static_cast<unsigned long>(result));
		}

		wil::com_ptr_nothrow<ITaskService> ConnectService(std::wstring& error)
		{
			wil::com_ptr_nothrow<ITaskService> service;
			HRESULT result = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service));
			if (SUCCEEDED(result))
			{
				result = service->Connect(MakeEmptyVariant(), MakeEmptyVariant(), MakeEmptyVariant(), MakeEmptyVariant());
			}
			if (FAILED(result))
			{
				error = DescribeHresult(L"Task Scheduler not available", result);
				return nullptr;
			}
			return service;
		}

		wil::com_ptr_nothrow<ITaskFolder> GetTaskFolder(ITaskService* service, bool create, std::wstring& error)
		{
			wil::com_ptr_nothrow<ITaskFolder> folder;
			if (SUCCEEDED(service->GetFolder(MakeBstr(TaskFolderPath).get(), &folder)))
			{
				return folder;
			}
			if (!create)
			{
				error = L"no AppTray task folder";
				return nullptr;
			}
			wil::com_ptr_nothrow<ITaskFolder> root;
			HRESULT result = service->GetFolder(MakeBstr(L"\\").get(), &root);
			if (SUCCEEDED(result))
			{
				result = root->CreateFolder(MakeBstr(TaskFolderName).get(), MakeEmptyVariant(), &folder);
			}
			if (FAILED(result))
			{
				error = DescribeHresult(L"cannot create the AppTray task folder", result);
				return nullptr;
			}
			return folder;
		}

		bool IsSamePath(const std::wstring& left, const std::wstring& right)
		{
			return CompareStringOrdinal(
					   left.c_str(), static_cast<int>(left.size()), right.c_str(), static_cast<int>(right.size()), TRUE)
				== CSTR_EQUAL;
		}

		std::wstring TakeBstr(HRESULT result, BSTR text)
		{
			const wil::unique_bstr owned(text);
			return SUCCEEDED(result) && text != nullptr ? std::wstring(text, SysStringLen(text)) : std::wstring{};
		}

		// Path and arguments of the task's (only) action
		bool GetTaskCommand(IRegisteredTask* task, std::wstring& program, std::wstring& arguments)
		{
			wil::com_ptr_nothrow<ITaskDefinition> definition;
			wil::com_ptr_nothrow<IActionCollection> actions;
			wil::com_ptr_nothrow<IAction> action;
			wil::com_ptr_nothrow<IExecAction> exec;
			if (FAILED(task->get_Definition(&definition)) || FAILED(definition->get_Actions(&actions))
				|| FAILED(actions->get_Item(1, &action)) || FAILED(action->QueryInterface(IID_PPV_ARGS(&exec))))
			{
				return false;
			}
			BSTR text = nullptr;
			HRESULT result = exec->get_Path(&text);
			program = TakeBstr(result, text);
			text = nullptr;
			result = exec->get_Arguments(&text);
			arguments = TakeBstr(result, text);
			return true;
		}
	}

	std::wstring MakeElevatedTaskName(std::wstring_view appId)
	{
		std::wstring name;
		std::uint32_t hash = 2166136261u; // FNV-1a of the id as AppTray compares ids (no case, '/')
		for (const wchar_t c : appId)
		{
			const wchar_t folded = c == L'\\' ? L'/' : static_cast<wchar_t>(std::towlower(c));
			hash = (hash ^ folded) * 16777619u;
			if (c == L'/' || c == L'\\')
			{
				name += L" - ";
			}
			else if (wcschr(L":*?\"<>|", c) == nullptr && c >= L' ')
			{
				name += c;
			}
		}
		return std::format(L"{} ({:08x})", name.substr(0, 180), hash);
	}

	std::wstring GetCurrentUserName()
	{
		wchar_t name[512]{};
		ULONG length = static_cast<ULONG>(std::size(name));
		if (GetUserNameExW(NameSamCompatible, name, &length))
		{
			return std::wstring(name, length);
		}
		return {};
	}

	bool RegisterElevatedTask(
		const std::wstring& name, const fs::path& program, const std::wstring& arguments, const std::wstring& user,
		std::wstring& error)
	{
		const wil::com_ptr_nothrow<ITaskService> service = ConnectService(error);
		if (!service)
		{
			return false;
		}
		const wil::com_ptr_nothrow<ITaskFolder> folder = GetTaskFolder(service.get(), true, error);
		if (!folder)
		{
			return false;
		}

		wil::com_ptr_nothrow<ITaskDefinition> definition;
		wil::com_ptr_nothrow<IRegistrationInfo> registration;
		wil::com_ptr_nothrow<IPrincipal> principal;
		wil::com_ptr_nothrow<ITaskSettings> settings;
		wil::com_ptr_nothrow<IActionCollection> actions;
		wil::com_ptr_nothrow<IAction> action;
		wil::com_ptr_nothrow<IExecAction> exec;
		HRESULT result = service->NewTask(0, &definition);
		if (SUCCEEDED(result))
		{
			result = definition->get_RegistrationInfo(&registration);
		}
		if (SUCCEEDED(result))
		{
			registration->put_Author(MakeBstr(L"AppTray").get());
			registration->put_Description(
				MakeBstr(L"Started by AppTray to open this app elevated without a UAC prompt.").get());
			result = definition->get_Principal(&principal);
		}
		if (SUCCEEDED(result))
		{
			// The user's own interactive session, with its full (elevated) token
			principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
			principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
			if (!user.empty())
			{
				principal->put_UserId(MakeBstr(user).get());
			}
			result = definition->get_Settings(&settings);
		}
		if (SUCCEEDED(result))
		{
			settings->put_AllowDemandStart(VARIANT_TRUE);
			settings->put_StartWhenAvailable(VARIANT_FALSE);
			settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
			settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
			settings->put_ExecutionTimeLimit(MakeBstr(L"PT0S").get()); // the app runs as long as it likes
			settings->put_MultipleInstances(TASK_INSTANCES_PARALLEL);
			settings->put_Priority(5); // normal (the default, 7, is below normal)
			result = definition->get_Actions(&actions);
		}
		if (SUCCEEDED(result))
		{
			result = actions->Create(TASK_ACTION_EXEC, &action);
		}
		if (SUCCEEDED(result))
		{
			result = action->QueryInterface(IID_PPV_ARGS(&exec));
		}
		if (SUCCEEDED(result))
		{
			exec->put_Path(MakeBstr(program.wstring()).get());
			if (!arguments.empty())
			{
				exec->put_Arguments(MakeBstr(arguments).get());
			}
			exec->put_WorkingDirectory(MakeBstr(program.parent_path().wstring()).get());

			VARIANT userId = MakeEmptyVariant();
			if (!user.empty())
			{
				userId.vt = VT_BSTR;
				userId.bstrVal = SysAllocString(user.c_str());
			}
			wil::com_ptr_nothrow<IRegisteredTask> registered;
			result = folder->RegisterTaskDefinition(
				MakeBstr(name).get(), definition.get(), TASK_CREATE_OR_UPDATE, userId, MakeEmptyVariant(),
				TASK_LOGON_INTERACTIVE_TOKEN, MakeEmptyVariant(), &registered);
			VariantClear(&userId);
		}
		if (FAILED(result))
		{
			error = DescribeHresult(L"cannot register the elevated task", result);
			return false;
		}
		return true;
	}

	bool DeleteElevatedTask(const std::wstring& name, std::wstring& error)
	{
		const wil::com_ptr_nothrow<ITaskService> service = ConnectService(error);
		if (!service)
		{
			return false;
		}
		const wil::com_ptr_nothrow<ITaskFolder> folder = GetTaskFolder(service.get(), false, error);
		if (!folder)
		{
			return true; // nothing to delete
		}
		const HRESULT result = folder->DeleteTask(MakeBstr(name).get(), 0);
		if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
		{
			error = DescribeHresult(L"cannot delete the elevated task", result);
			return false;
		}
		return true;
	}

	ElevatedTaskRun RunElevatedTask(
		const std::wstring& name, const fs::path& program, const std::wstring& arguments, std::wstring& error)
	{
		const wil::com_ptr_nothrow<ITaskService> service = ConnectService(error);
		if (!service)
		{
			return ElevatedTaskRun::Failed;
		}
		const wil::com_ptr_nothrow<ITaskFolder> folder = GetTaskFolder(service.get(), false, error);
		if (!folder)
		{
			return ElevatedTaskRun::Failed;
		}
		wil::com_ptr_nothrow<IRegisteredTask> task;
		HRESULT result = folder->GetTask(MakeBstr(name).get(), &task);
		if (FAILED(result))
		{
			error = DescribeHresult(L"no elevated task", result);
			return ElevatedTaskRun::Failed;
		}
		// The app's executable or arguments changed since the task was made (a rescan picked a new
		// exe, user.json edited by hand): the task would open the old one, or nothing at all
		std::wstring taskProgram;
		std::wstring taskArguments;
		if (!GetTaskCommand(task.get(), taskProgram, taskArguments) || !IsSamePath(taskProgram, program.wstring())
			|| taskArguments != arguments)
		{
			error = std::format(L"the task opens \"{}\" {}", taskProgram, taskArguments);
			return ElevatedTaskRun::Stale;
		}
		wil::com_ptr_nothrow<IRunningTask> running;
		result = task->Run(MakeEmptyVariant(), &running);
		if (FAILED(result))
		{
			error = DescribeHresult(L"cannot start the elevated task", result);
			return ElevatedTaskRun::Failed;
		}
		return ElevatedTaskRun::Started;
	}
}
