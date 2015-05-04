# IEDiagnosticsAdapter
IE Diagnostics Adapter is a proxy that enables tools to debug and diagnose IE using the Chrome remote debug protocol.

### Current Release

https://github.com/Microsoft/IEDiagnosticsAdapter/releases/tag/v0.1.4

## Installing
Simply download a release on to your hard drive.

1. Download the current release
2. Extract/Copy the files into a folder on disk where you wish to run the file from. We currently recommend a folder on the desktop.
3. You should end up with IEDiagnosticsAdapter.exe, Proxy.dll, and Proxy64.dll files inside the folder
4. If you plan to use this tool against IE in *Enhanced Protected Mode* (note, this includes 64 bit machines), 
    If you're on Windows 8 or later, navigate to the directory containing **Proxy64.dll** and type `icacls proxy64.dll /grant "ALL APPLICATION PACKAGES":(RX)` in an Administrator Command Prompt to grant necessary permissions. 

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
In order to use the IE Diagnostics Adapter you need to have IE11 installed.

## Supported Chrome Remote APIs
The table below is a summary of the support for the various [Chrome remote debugging v1.1 protocol](https://developer.chrome.com/devtools/docs/debugger-protocol). Right now the IE Diagnostics Adapter just supports the core debugging feature set. More detailed information can be found on the [wiki](https://github.com/Microsoft/IEDiagnosticsAdapter/wiki/Supported-API-Set).  

## Building & Contributing
To build and contribute to this project take a gander at the wiki pages on [building](https://github.com/Microsoft/IEDiagnosticsAdapter/wiki/Building) and [contributing](https://github.com/Microsoft/IEDiagnosticsAdapter/wiki/Contributing) 
