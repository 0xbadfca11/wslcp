#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define STRICT_GS_ENABLED
#define _ATL_NO_AUTOMATIC_NAMESPACE
#define _ATL_NO_DEFAULT_LIBS
#define _ATL_NO_WIN_SUPPORT
#define _CRTDBG_MAP_ALLOC
#pragma warning(suppress: 4117)
#define _CRT_USE_BUILTIN_OFFSETOF
#include <windows.h>
#include <atlbase.h>
#include <atlchecked.h>
#include <atlsecurity.h>
#include <aclapi.h>
#include <initguid.h>
#include <pathcch.h>
#include <shobjidl.h>
#include <virtdisk.h>
#include <clocale>
#include <cstdio>
#include <memory>
#include <crtdbg.h>
#pragma comment(lib, "pathcch")
#pragma comment(lib, "shlwapi")
#pragma comment(lib, "virtdisk")

[[noreturn]]
void usage()
{
	puts(
		"WSL CheckPoint manager\n"
		"\n"
		"wslcp c[heckpoint] [<DistributionName>]\n"
		"wslcp r[evert]     [<DistributionName>]\n"
		"wslcp d[elete]     [<DistributionName>]\n"
		"wslcp s[ave]       [<DistributionName>]\n"
		"wslcp m[erge]      [<DistributionName>]\n"
		"\n"
		"  checkpoint          Create new checkpoint.\n"
		"  revert              Revert to checkpoint.\n"
		"                      A new checkpoint is created after revert.\n"
		"  delete              Revert to checkpoint.\n"
		"                      Checkpoint will delete after revert.\n"
		"  save                Merge content written since checkpoint.\n"
		"                      A new checkpoint is created after merge.\n"
		"  merge               Merge content written since checkpoint.\n"
		"                      Checkpoint will delete after merge.\n"
		"  <DistributionName>  Specify the operation target distribution.\n"
		"                      If omitted, the default distribution will be targeted."
	);
	ExitProcess(EXIT_FAILURE);
}
[[noreturn]]
void die(_In_z_ PCWSTR msg)
{
	fputws(msg, stderr);
	fputws(L"\n", stderr);
	_ASSERT(!IsDebuggerPresent());
	ExitProcess(EXIT_FAILURE);
}
[[noreturn]]
void die(_In_ ULONG error_code = GetLastError())
{
	PWSTR msg;
	FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		error_code,
		0,
		reinterpret_cast<PWSTR>(&msg),
		0,
		nullptr
	);
	fputws(msg, stderr);
	if (error_code == ERROR_ACCESS_DENIED)
	{
		fputws(L"Can not operation while the WSL is running. Has terminated ?\n", stderr);
	}
	_ASSERT(!IsDebuggerPresent());
	ExitProcess(EXIT_FAILURE);
}
struct WSLCheckPoint
{
	WCHAR target_vhdx[MAX_PATH];
	WCHAR parent_vhdx[MAX_PATH];
	WSLCheckPoint(_In_opt_z_ PCWSTR distribution_name)
	{
		LSTATUS result;
		ATL::CRegKey lxss_key;
		result = lxss_key.Open(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Lxss)", KEY_READ);
		if (result != ERROR_SUCCESS)
		{
			die(result);
		}
		ATL::CRegKey distribution_key;
		if (distribution_name == nullptr)
		{
			WCHAR distribution_guid[MAX_PATH];
			ULONG key_name_length = ARRAYSIZE(distribution_guid);
			result = lxss_key.QueryStringValue(L"DefaultDistribution", distribution_guid, &key_name_length);
			if (result != ERROR_SUCCESS)
			{
				die(result);
			}
			result = distribution_key.Open(lxss_key, distribution_guid, KEY_READ);
			if (result != ERROR_SUCCESS)
			{
				die(result);
			}
		}
		else
		{
			for (ULONG i = 0;; i++)
			{
				WCHAR distribution_guid[MAX_PATH];
				ULONG key_name_length = ARRAYSIZE(distribution_guid);
				result = lxss_key.EnumKey(i, distribution_guid, &key_name_length);
				if (result == ERROR_NO_MORE_ITEMS)
				{
					die(L"The specified distribution not found.");
				}
				else if (result != ERROR_SUCCESS)
				{
					die(result);
				}
				result = distribution_key.Open(lxss_key, distribution_guid, KEY_READ);
				if (result != ERROR_SUCCESS)
				{
					die(result);
				}
				WCHAR distribution_name_data[MAX_PATH];
				ULONG distribution_value_length = ARRAYSIZE(distribution_name_data);
				result = distribution_key.QueryStringValue(L"DistributionName", distribution_name_data, &distribution_value_length);
				if (result != ERROR_SUCCESS)
				{
					die(result);
				}
				if (_wcsicmp(distribution_name_data, distribution_name) == 0)
				{
					break;
				}
			}
		}
		WCHAR base_path[MAX_PATH];
		ULONG path_value_length = ARRAYSIZE(base_path);
		result = distribution_key.QueryStringValue(L"BasePath", base_path, &path_value_length);
		if (result != ERROR_SUCCESS)
		{
			die(result);
		}
		ATLENSURE_SUCCEEDED(PathCchCombine(target_vhdx, ARRAYSIZE(target_vhdx), base_path, L"ext4.vhdx"));
		if (!PathFileExistsW(target_vhdx))
		{
			die(L"The distribution is not WSL2.");
		}
		ATL::AtlCrtErrorCheck(wcscpy_s(parent_vhdx, target_vhdx));
		ATLENSURE_SUCCEEDED(PathCchRenameExtension(parent_vhdx, ARRAYSIZE(target_vhdx), L"avhdx"));
	}
	bool IsDifferencingDisk()
	{
		ULONG result;
		VIRTUAL_STORAGE_TYPE virt_storage_type = { VIRTUAL_STORAGE_TYPE_DEVICE_VHDX, VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT };
		OPEN_VIRTUAL_DISK_PARAMETERS open_virtual_disk_parameters = { OPEN_VIRTUAL_DISK_VERSION_1 };
		ATL::CHandle virt_disk;
		result = OpenVirtualDisk(
			&virt_storage_type,
			target_vhdx,
			VIRTUAL_DISK_ACCESS_GET_INFO,
			OPEN_VIRTUAL_DISK_FLAG_NO_PARENTS,
			&open_virtual_disk_parameters,
			&virt_disk.m_h
		);
		if (result != ERROR_SUCCESS)
		{
			die(result);
		}
		GET_VIRTUAL_DISK_INFO virt_disk_info;
		virt_disk_info.Version = GET_VIRTUAL_DISK_INFO_PROVIDER_SUBTYPE;
		ULONG virt_disk_info_size = sizeof virt_disk_info;
		result = GetVirtualDiskInformation(virt_disk, &virt_disk_info_size, &virt_disk_info, nullptr);
		if (result != ERROR_SUCCESS)
		{
			die(result);
		}
		return virt_disk_info.ProviderSubtype == 4;
	}
	WSLCheckPoint& CreateCheckPoint()
	{
		if (PathFileExistsW(target_vhdx))
		{
			if (IsDifferencingDisk())
			{
				die(L"Checkpoint already exists.");
			}
			ATL::CHandle f(CreateFileW(target_vhdx, DELETE | READ_CONTROL | WRITE_DAC, 0, nullptr, OPEN_EXISTING, 0, nullptr));
			if (f == INVALID_HANDLE_VALUE)
			{
				f.Detach();
				die();
			}
			PSID sid;
			if (!ConvertStringSidToSidW(L"S-1-5-83-0", &sid))
			{
				die();
			}
			ULONG result;
			PACL dacl;
			PSECURITY_DESCRIPTOR security_descriptor;
			result = GetSecurityInfo(
				f,
				SE_FILE_OBJECT,
				DACL_SECURITY_INFORMATION,
				nullptr,
				nullptr,
				&dacl,
				nullptr,
				&security_descriptor
			);
			if (result != ERROR_SUCCESS)
			{
				die(result);
			}
			EXPLICIT_ACCESS_W explicit_access = {
				GENERIC_READ,
				SET_ACCESS,
				NO_INHERITANCE,
				{ nullptr, NO_MULTIPLE_TRUSTEE, TRUSTEE_IS_SID, TRUSTEE_IS_WELL_KNOWN_GROUP, (PWSTR)sid }
			};
			result = SetEntriesInAclW(1, &explicit_access, dacl, &dacl);
			if (result != ERROR_SUCCESS)
			{
				die(result);
			}
			result = SetSecurityInfo(
				f,
				SE_FILE_OBJECT,
				DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
				nullptr,
				nullptr,
				dacl,
				nullptr
			);
			if (result != ERROR_SUCCESS)
			{
				die(result);
			}
			LocalFree(sid);
			LocalFree(security_descriptor);
			LocalFree(dacl);
			union
			{
				FILE_RENAME_INFO file_rename_info;
				BYTE buffer[offsetof(FILE_RENAME_INFO, FileName[MAX_PATH])];
			};
			file_rename_info.ReplaceIfExists = FALSE;
			file_rename_info.RootDirectory = nullptr;
			file_rename_info.FileNameLength = (ULONG)wcslen(parent_vhdx) * sizeof(WCHAR);
			ATL::Checked::wcscpy_s(file_rename_info.FileName, MAX_PATH, parent_vhdx);
			if (!SetFileInformationByHandle(f, FileRenameInfo, &buffer, sizeof buffer))
			{
				die();
			}
		}
		else if (!PathFileExistsW(parent_vhdx))
		{
			die(L"The disk image file not found.\nSomething is wrong.");
		}
		VIRTUAL_STORAGE_TYPE virt_storage_type = { VIRTUAL_STORAGE_TYPE_DEVICE_VHDX, VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT };
		CREATE_VIRTUAL_DISK_PARAMETERS create_virt_disk_params;
		create_virt_disk_params.Version = CREATE_VIRTUAL_DISK_VERSION_1;
		create_virt_disk_params.Version1.UniqueId = GUID_NULL;
		create_virt_disk_params.Version1.MaximumSize = 0;
		create_virt_disk_params.Version1.BlockSizeInBytes = CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_BLOCK_SIZE;
		create_virt_disk_params.Version1.SectorSizeInBytes = CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_SECTOR_SIZE;
		create_virt_disk_params.Version1.ParentPath = parent_vhdx;
		create_virt_disk_params.Version1.SourcePath = nullptr;
		ATL::CHandle virt_disk;
		ULONG result = CreateVirtualDisk(
			&virt_storage_type,
			target_vhdx,
			VIRTUAL_DISK_ACCESS_CREATE,
			nullptr,
			CREATE_VIRTUAL_DISK_FLAG_NONE,
			0,
			&create_virt_disk_params,
			nullptr,
			&virt_disk.m_h
		);
		if (result != ERROR_SUCCESS)
		{
			die(result);
		}
		puts("Checkpoint created.");
		return *this;
	}
	WSLCheckPoint& DeleteCheckPoint()
	{
		if (!IsDifferencingDisk())
		{
			die(L"Checkpoint is not exist.");
		}
		else
		{
			if (!DeleteFileW(target_vhdx))
			{
				die();
			}
			if (!MoveFileW(parent_vhdx, target_vhdx))
			{
				die();
			}
			puts("Checkpoint deleted.");
		}
		return *this;
	}
	WSLCheckPoint& MergeCheckPoint()
	{
		if (!IsDifferencingDisk())
		{
			die(L"Checkpoint is not exist.");
		}
		ULONG result;
		VIRTUAL_STORAGE_TYPE virt_storage_type = { VIRTUAL_STORAGE_TYPE_DEVICE_VHDX, VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT };
		OPEN_VIRTUAL_DISK_PARAMETERS open_virtual_disk_parameters = { OPEN_VIRTUAL_DISK_VERSION_2 };
		ATL::CHandle virt_disk;
		result = OpenVirtualDisk(
			&virt_storage_type,
			target_vhdx,
			VIRTUAL_DISK_ACCESS_NONE,
			OPEN_VIRTUAL_DISK_FLAG_NONE,
			&open_virtual_disk_parameters,
			&virt_disk.m_h
		);
		if (result != ERROR_SUCCESS)
		{
			die(result);
		}
		MERGE_VIRTUAL_DISK_PARAMETERS merge_virtual_disk_parameters;
		merge_virtual_disk_parameters.Version = MERGE_VIRTUAL_DISK_VERSION_1;
		merge_virtual_disk_parameters.Version1.MergeDepth = 1;
		OVERLAPPED o = {};
		result = MergeVirtualDisk(
			virt_disk,
			MERGE_VIRTUAL_DISK_FLAG_NONE,
			&merge_virtual_disk_parameters,
			&o
		);
		if (result != ERROR_SUCCESS && result != ERROR_IO_PENDING)
		{
			die(result);
		}
		puts("Checkpoint merging in progress.");
		ATL::CComPtr<ITaskbarList3> TaskbarList;
		ATLENSURE_SUCCEEDED(TaskbarList.CoCreateInstance(CLSID_TaskbarList));
		const HWND console_hwnd = GetConsoleWindow();
		for (;;)
		{
			VIRTUAL_DISK_PROGRESS virt_disk_progress;
			// MUST NOT USE OPEN_VIRTUAL_DISK_VERSION_1 !!!!!
			result = GetVirtualDiskOperationProgress(virt_disk, &o, &virt_disk_progress);
			if (result != ERROR_SUCCESS)
			{
				TaskbarList->SetProgressState(console_hwnd, TBPF_NOPROGRESS);
				die(result);
			}
			if (virt_disk_progress.OperationStatus != ERROR_IO_PENDING)
			{
				break;
			}
			TaskbarList->SetProgressValue(
				console_hwnd,
				static_cast<ULONGLONG>(virt_disk_progress.CurrentValue * 1. / virt_disk_progress.CompletionValue * 100),
				100
			);
			Sleep(100);
		}
		TaskbarList->SetProgressState(console_hwnd, TBPF_NOPROGRESS);
		puts("Checkpoint merged.");
		virt_disk.Close();
		DeleteCheckPoint();
		return *this;
	}
};
int wmain(int argc, PWSTR argv[])
{
	ATLENSURE(SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32));
	_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
	setlocale(LC_ALL, "");

	ATLENSURE_SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE));
	ATL::CComPtr<ITaskbarList3> TaskbarList;
	ATLENSURE_SUCCEEDED(TaskbarList.CoCreateInstance(CLSID_TaskbarList));
	TaskbarList->SetProgressState(GetConsoleWindow(), TBPF_NOPROGRESS);

	if (argc < 2)
	{
		usage();
	}
	if (_wcsicmp(argv[1], L"c") == 0 || _wcsicmp(argv[1], L"checkpoint") == 0)
	{
		WSLCheckPoint(argv[2]).CreateCheckPoint();
	}
	else if (_wcsicmp(argv[1], L"r") == 0 || _wcsicmp(argv[1], L"revert") == 0)
	{
		WSLCheckPoint(argv[2]).DeleteCheckPoint().CreateCheckPoint();
	}
	else if (_wcsicmp(argv[1], L"d") == 0 || _wcsicmp(argv[1], L"delete") == 0)
	{
		WSLCheckPoint(argv[2]).DeleteCheckPoint();
	}
	else if (_wcsicmp(argv[1], L"m") == 0 || _wcsicmp(argv[1], L"merge") == 0)
	{
		WSLCheckPoint(argv[2]).MergeCheckPoint();
	}
	else if (_wcsicmp(argv[1], L"s") == 0 || _wcsicmp(argv[1], L"save") == 0)
	{
		WSLCheckPoint(argv[2]).MergeCheckPoint().CreateCheckPoint();
	}
	else
	{
		usage();
	}
}