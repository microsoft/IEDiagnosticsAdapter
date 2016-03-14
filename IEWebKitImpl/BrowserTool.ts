//
// Copyright (C) Microsoft. All rights reserved.
//

/// <reference path="IE11.DiagnosticOM.d.ts" />
/// <reference path="Interfaces.d.ts"/>

/// Proxy to hande the page domain of the Chrome remote debug protocol 
module IEDiagnosticsAdapter {
    export class BrowserTool implements IDomainHandler {
        constructor() {
        }

        public processMessage(method: string, request: IWebKitRequest): void {
            var processedResult: IWebKitResult;

            switch (method) {
                case "registerEditor":
                    processedResult = this.registerEditor(request);
                    break;

                default:
                    processedResult = null;
                    break;
            }

            browserHandler.postResponse(request.id, processedResult);
        }

        private registerEditor(request: IWebKitRequest): IWebKitResult {
            var processedResult: IWebKitResult = {};

            try {
                var base64Logo: string = request.params.base64Logo;
                var displayName: string = request.params.displayName;
                var name: string = request.params.name;


            } catch (ex) {
                processedResult = {
                    error: ex
                };
            }

            return processedResult;
        }


        private EDITOR_GLOBAL: string = "__Editor";
        private EDITOR_OPENDOCUMENT_EVENTNAME: string = "__Editor.openDocument";
        private BROWERTOOL_REGISTEREDITOR_EVENTNAME: string = "__BrowserTool.registerEditor";
        private notifyPageOfEditor(base64Logo: string, displayName: string, name: string) {
            // The approach to register an editor is
            // 1) Set the global (there can only be one editor
            // 2) Wire up an event handler for Editor.OpenDOcument
            // 3) Fire the registration event
            
            browser.document.defaultView[this.EDITOR_GLOBAL] = {
                base64Logo: base64Logo,
                displayName: displayName,
                name: name
            };

            browser.document.defaultView.addEventListener(this.EDITOR_OPENDOCUMENT_EVENTNAME, (e: any) => this.handleOpenDocument);

            var registorEditorEvent = browser.document.createEvent(this.BROWERTOOL_REGISTEREDITOR_EVENTNAME);
            browser.document.dispatchEvent(registorEditorEvent);
        }

        private handleOpenDocument(event: any): void {
            var url: string = event.url;
            var startLine: number = event.startLine;
            var startColumn: number = event.startColumn;

            browserHandler.postNotification("editor.openDocument", {
                url: url,
                startLine: startLine,
                startColumn: startColumn
            });
        }
    }

    export var browserToolHandler: BrowserTool = new BrowserTool();
}