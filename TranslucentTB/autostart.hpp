#pragma once
#ifndef AUTOSTART_HPP
#define AUTOSTART_HPP

#ifndef STORE
#include <cwchar>
#include <Shlwapi.h>
#include <winbase.h>
#include <winreg.h>

#include "app.hpp"
#else
#define _HIDE_GLOBAL_ASYNC_STATUS
#include <roapi.h>
#include <Windows.ApplicationModel.h>
#include <wrl/wrappers/corewrappers.h>

#include "SynchronousOperation.hpp"
#endif

#include "ttberror.hpp"

namespace Autostart {

	enum class StartupState {
		Disabled = 0,
		DisabledByUser = 1,
		Enabled = 2
	};

#ifdef STORE
	// Send help
	Microsoft::WRL::ComPtr<ABI::Windows::ApplicationModel::IStartupTask> GetApplicationStartupTask()
	{
		using namespace ABI::Windows;

		static Microsoft::WRL::ComPtr<ApplicationModel::IStartupTask> task;

		if (!task)
		{
			using namespace Microsoft::WRL;
			using namespace Microsoft::WRL::Wrappers;
			typedef Foundation::Collections::IVectorView<ApplicationModel::StartupTask *> StartupTasksVector;

			ComPtr<ApplicationModel::IStartupTaskStatics> startup_tasks_statics;
			if (!Error::Handle(Foundation::GetActivationFactory<ComPtr<ApplicationModel::IStartupTaskStatics>>(HStringReference(RuntimeClass_Windows_ApplicationModel_StartupTask).Get(), &startup_tasks_statics), Error::Level::Log, L"Activating IStartupTaskStatics instance failed."))
			{
				return nullptr;
			}

			ComPtr<Foundation::IAsyncOperation<StartupTasksVector *>> operation;
			if (!Error::Handle(startup_tasks_statics->GetForCurrentPackageAsync(operation.GetAddressOf()), Error::Level::Log, L"Starting acquisition of package startup tasks failed."))
			{
				return nullptr;
			}

			// Fuck off async
			ComPtr<StartupTasksVector> package_tasks;
			if (!Error::Handle(SynchronousOperation<StartupTasksVector *>(operation.Get()).GetResults(package_tasks.GetAddressOf()), Error::Level::Log, L"Acquiring package startup tasks failed."))
			{
				return nullptr;
			}

			if (!Error::Handle(package_tasks->GetAt(0, task.GetAddressOf()), Error::Level::Log, L"Getting first package startup task failed."))
			{
				return nullptr;
			}
		}

		return task;
	}
#endif

	StartupState GetStartupState()
	{
#ifndef STORE
		LRESULT error = RegGetValue(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", App::NAME.c_str(), RRF_RT_REG_SZ, NULL, NULL, NULL);
		if (error == ERROR_FILE_NOT_FOUND)
		{
			return StartupState::Disabled;
		}
		else if (error == ERROR_SUCCESS)
		{
			uint8_t status[12];
			DWORD size = sizeof(status);
			error = RegGetValue(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run", App::NAME.c_str(), RRF_RT_REG_BINARY, NULL, &status, &size);
			if (error != ERROR_FILE_NOT_FOUND && Error::Handle(HRESULT_FROM_WIN32(error), Error::Level::Log, L"Querying startup disable state failed.") && status[0] == 3)
			{
				return StartupState::DisabledByUser;
			}
			else
			{
				return StartupState::Enabled;
			}
		}
		else
		{
			Error::Handle(HRESULT_FROM_WIN32(error), Error::Level::Log, L"Querying startup state failed.");
			return StartupState::Disabled;
		}
#else
		auto task = GetApplicationStartupTask();
		if (!task)
		{
			return StartupState::Disabled;
		}

		ABI::Windows::ApplicationModel::StartupTaskState state;
		if (!Error::Handle(task->get_State(&state), Error::Level::Log, L"Could not retrieve startup task state"))
		{
			return StartupState::Disabled;
		}
		else
		{
			return static_cast<StartupState>(state);
		}
#endif
	}

	void SetStartupState(const StartupState &state)
	{
#ifndef STORE
		HKEY hkey;
		LRESULT error = RegCreateKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", &hkey);
		if (Error::Handle(HRESULT_FROM_WIN32(error), Error::Level::Error, L"Opening registry key failed!")) //Creates a key
		{
			if (state == StartupState::Enabled)
			{
				HMODULE hModule = GetModuleHandle(NULL);
				wchar_t path[MAX_PATH];
				GetModuleFileName(hModule, path, MAX_PATH);
				PathQuoteSpaces(path);

				error = RegSetValueEx(hkey, App::NAME.c_str(), 0, REG_SZ, reinterpret_cast<BYTE *>(path), wcslen(path) * sizeof(wchar_t));
				Error::Handle(HRESULT_FROM_WIN32(error), Error::Level::Error, L"Error while setting startup registry value!");
			}
			else if (state == StartupState::Disabled)
			{
				error = RegDeleteValue(hkey, App::NAME.c_str());
				Error::Handle(HRESULT_FROM_WIN32(error), Error::Level::Error, L"Error while deleting startup registry value!");
			}
			error = RegCloseKey(hkey);
			Error::Handle(HRESULT_FROM_WIN32(error), Error::Level::Log, L"Error closing registry key.");
		}
#else
		auto task = GetApplicationStartupTask();
		if (!task)
		{
			return;
		}

		if (state == StartupState::Enabled)
		{
			Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::ApplicationModel::StartupTaskState>> operation;
			if (!Error::Handle(task->RequestEnableAsync(operation.GetAddressOf()), Error::Level::Log, L"Could not start setting of startup task state"))
			{
				return;
			}

			ABI::Windows::ApplicationModel::StartupTaskState new_state;
			Error::Handle(SynchronousOperation<ABI::Windows::ApplicationModel::StartupTaskState>(operation.Get()).GetResults(&new_state), Error::Level::Log, L"Could not set new startup task state");
		}
		else if (state == StartupState::Disabled)
		{
			Error::Handle(task->Disable(), Error::Level::Log, L"Could not disable startup task state");
		}
#endif
	}

}

#endif // !AUTOSTART_HPP