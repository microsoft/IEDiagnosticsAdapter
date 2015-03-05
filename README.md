# IEDiagnosticsAdapter
IE Diagnostics Adapter is a proxy that enables tools to debug and diagnose IE using the Chrome remote debug protocol. 

## Installing
Simply download and unpack a release on to your hard drive.

1. Download the current release
2. Copy into a folder on disk e.g. `%HOMEPATH%\Desktop`
3. Extract the files
4. Delete the zip file

## Running

1. Launch IE and browse to the site you want to debug 
2. Run the IEDiagnosticsAdapter.exe

## Connecting

### Brackets
<Coming soon...>

### Chrome Dev Tools

1. Open Chrome browse to http://localhost:9222/
2. Choose a page to debug

## Supported Chrome Remote APIs
The table below is a summary of the support for the various [Chrome remote debugging v1.1 protocol](https://developer.chrome.com/devtools/docs/debugger-protocol). Right now the IE Diagnostics Adapter just supports the core debugging feature set. More detailed information can be found in the wiki.  

API Family | Support
------------ | -------------
Console | None
Debugging | Almost everything
DOM | None
DOM Debugger | None
Input | None
Network | None
Page | None
Runtime | None
Timeline | None

## Building & Contributing
<Coming soon...>
