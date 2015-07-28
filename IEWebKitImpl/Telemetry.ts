module Proxy {

    // All public functions are catch exceptions as telemetry isn't critical and erros can be ignored.
    export class TelemetryHandler {
        public static APP_ID: string = "IEDiagnosticAdapater";
        public static VERSION: string = "1.0.0.1";
        public static OT_ENDPOINT_URL: string = "http://localhost:3000/session";

        public clientId:string;
        public appId: string;
        public appVersion: string;

        private _currentSession: Session;

        constructor() {
            try {
                this.appId = TelemetryHandler.APP_ID
                this.appVersion = TelemetryHandler.VERSION;
            } catch (ex) {
                
            }         
        }

        public startSession(): void {
            try {
                this.clientId = this.getClientId();
                this._currentSession = new Session(this.clientId, this.appId, this.appVersion);
                this._currentSession.start();
            } catch (ex) {
                
            }
        }

        public stopSession(): void {
            try{
                this._currentSession.end();
                this.sendSession(this._currentSession);
            } catch(ex) {
                
            }
        }

        public track(command: string): void {
            try {
                if (this._currentSession) {
                    this._currentSession.track(command);
                }
            } catch(ex) {
               
            }
        }

        private sendSession(sessionToSend:Session): void {
            var data = "";

            data = "startDate="     + sessionToSend.startDate.toString();;
            data = "&duration="     + sessionToSend.duration;
            data = "&clientId="     + sessionToSend.clientId;
            data = "&appId="        + sessionToSend.appId;
            data = "&appVersion="   + sessionToSend.appVersion;
            data = "&payload="      + JSON.stringify(sessionToSend.payload);

            var xhr = new XMLHttpRequest();

            xhr.addEventListener("readystatechange", function () {
                // The response doesn't matter
            });

            xhr.open("POST", TelemetryHandler.OT_ENDPOINT_URL, false);

            xhr.send(data);
        }

        private getClientId(): string {
            var clientId: string = "Temp";

            return clientId;
        }
    }

    export class Session {
        public startDate: Date;
        public duration: number;
        public clientId: string;
        public appId: string;
        public appVersion: string;
        public payload: any;

        constructor(clientId: string, appId: string, appVersion: string) {
            this.clientId = clientId;
            this.appId = appId;
            this.appVersion = appVersion;
        }

        public start(): void {
            this.startDate = new Date();
            this.payload = {};
        }

        public end():void {
            var currentDate: Date = new Date();
            this.duration = currentDate.getTime() - this.startDate.getTime();
        }

        public track(command:string):void {
            if (!this.payload[command]) {
                this.payload[command] = 1;
            } else {
                this.payload[command]++;
            }
        }
    }


    export var telemetryHandler: TelemetryHandler = new TelemetryHandler();
}