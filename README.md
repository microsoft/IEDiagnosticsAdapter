# IEDiagnosticsAdapter
IE Diagnostics Adapter is a proxy that enables tools to debug and diagnose IE using the Chrome remote debug protocol.

### Current Release

https://github.com/Microsoft/IEDiagnosticsAdapter/releases/tag/v0.1.1-pre-alpha

## Installing
Simply download and unpack a release on to your hard drive.

1. Download the [current release](https://github.com/Microsoft/IEDiagnosticsAdapter/releases/download/v0.1.1-pre-alpha/IEDiagnosticsAdapter-v0.1.1-pre-alpha.zip)
2. Copy into a folder on disk where you wish to run the file from e.g. `%HOMEPATH%\Desktop`
3. Extract the files
4. Delete the zip file
5. If you plan to use this tool against IE in Enhanced Protected Mode, navigate to the directory containing proxy.dll and type "icacls proxy.dll /grant "ALL APPLICATION PACKAGES":(RX)" to grant necessary permissions

## Running

1. Launch IE and browse to the site you want to debug 
2. Run the IEDiagnosticsAdapter.exe

## Connecting

### Brackets
<Coming soon...>

### Chrome Dev Tools

1. Open Chrome browse to http://localhost:9222/
2. Choose a page to debug

## Required Browsers
In order to use the IE Diagnostics Adpater you need to have IE11 installed.

## Supported Chrome Remote APIs
The table below is a summary of the support for the various [Chrome remote debugging v1.1 protocol](https://developer.chrome.com/devtools/docs/debugger-protocol). Right now the IE Diagnostics Adapter just supports the core debugging feature set. More detailed information can be found on the [wiki](https://github.com/Microsoft/IEDiagnosticsAdapter/wiki/Supported-API-Set).  

## Building & Contributing
To build and contribute to this project take a gander at the wiki pages on [building](https://github.com/Microsoft/IEDiagnosticsAdapter/wiki/Building) and [contributing](https://github.com/Microsoft/IEDiagnosticsAdapter/wiki/Contributing) 
