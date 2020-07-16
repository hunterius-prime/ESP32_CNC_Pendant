#include "InetServer.h"

#include <ESPmDNS.h>
#include <SD.h>
#include <WiFi.h>

#include "Job.h"

#define API_VERSION     "0.1"
#define SKETCH_VERSION  "0.0.1"



void WebServer::begin() {

    /*
    File cfg = SD.open("/config.txt");
    String essid = cfg.readStringUntil('\n'); essid.trim();
    String pass = cfg.readStringUntil('\n'); pass.trim();
    cfg.close();
    WiFi.begin(essid.c_str(), pass.c_str() );
    */

    WiFi.begin("***REMOVED***", "***REMOVED***");

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    MDNS.begin("cncpendant");
    MDNS.setInstanceName("cncpendantinstance");

    // OctoPrint API
    // Unfortunately, Slic3r doesn't seem to recognize it
    MDNS.addService("octoprint", "tcp", port);
    MDNS.addServiceTxt("octoprint", "tcp", "path", "/");
    MDNS.addServiceTxt("octoprint", "tcp", "api", API_VERSION);
    MDNS.addServiceTxt("octoprint", "tcp", "version", SKETCH_VERSION);

    MDNS.addService("http", "tcp", port);
    MDNS.addServiceTxt("http", "tcp", "path", "/");
    MDNS.addServiceTxt("http", "tcp", "api", API_VERSION);
    MDNS.addServiceTxt("http", "tcp", "version", SKETCH_VERSION);

    registerOptoPrintApi();

    registerWebBrowser();

    server.begin();

}


void WebServer::registerOptoPrintApi() {
    
    server.on("/api/login", HTTP_POST, [](AsyncWebServerRequest * request) {
        // https://docs.octoprint.org/en/master/api/general.html#post--api-login
        // https://github.com/fieldOfView/Cura-OctoPrintPlugin/issues/155#issuecomment-596109663
        Serial.printf("/api/login");
        request->send(200, "application/json", "{}");  
    });

    server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest * request) {
        Serial.printf("/api/version");
        // http://docs.octoprint.org/en/master/api/version.html
        request->send(200, "application/json", "{\r\n"
                "  \"api\": \"" API_VERSION "\",\r\n"
                "  \"server\": \"" SKETCH_VERSION "\"\r\n"
                "}");  
    });

    server.on("/api/connection", HTTP_GET, [](AsyncWebServerRequest * request) {
        Serial.printf("/api/connection");
        // http://docs.octoprint.org/en/master/api/connection.html#get-connection-settings
        request->send(200, "application/json", "{\r\n"
                "  \"current\": {\r\n"
                //"    \"state\": \"" + getState() + "\",\r\n"
                "    \"state\": \"Operational\",\r\n"
                "    \"port\": \"Serial\",\r\n"
                "    \"baudrate\": 115200,\r\n"
                "    \"printerProfile\": \"Default\"\r\n"
                "  },\r\n"
                "  \"options\": {\r\n"
                "    \"ports\": \"Serial\",\r\n"
                "    \"baudrates\": [115200, 250000],\r\n"
                "    \"printerProfiles\": \"Default\",\r\n"
                "    \"portPreference\": \"Serial\",\r\n"
                "    \"baudratePreference\": 115200,\r\n"
                "    \"printerProfilePreference\": \"Default\",\r\n"
                "    \"autoconnect\": true\r\n"
                "  }\r\n"
                "}");
    });


    // File Operations
    // Pending: http://docs.octoprint.org/en/master/api/files.html#retrieve-all-files
    server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest * request) {
        Serial.printf("/api/files");
        request->send(200, "application/json", "{\r\n"
                "  \"files\": ["
                "    { \"name\": \"whistle_.gco\", \"path\":\"whistle_.gco\" }\r\n"
                "  ]\r\n"
                "}");
    });

    server.on("/api/files/local", HTTP_POST, [this](AsyncWebServerRequest * request) {
        Serial.printf("POST %s\n", request->url().c_str() );

        //if( request->hasHeader("Content-Type") ) Serial.println(request->getHeader("Content-Type")->value() );

        if(request->hasParam("select", true) && request->getParam("select", true)->value()=="true") {
            Job *job = Job::getJob();
            job->setFile(uploadedFilePath);
        }
        if(request->hasParam("print", true) && request->getParam("print", true)->value()=="true") { 
            Job *job = Job::getJob();
            job->start();
        } // print now


        // OctoPrint sends 201 here; https://github.com/fieldOfView/Cura-OctoPrintPlugin/issues/155#issuecomment-596110996
        AsyncWebServerResponse *response = request->beginResponse(201, "application/json", "{\r\n"
                "  \"files\": {\r\n"
                "    \"local\": {\r\n"
                "      \"name\": \"" + uploadedFilePath + "\",\r\n"
                "      \"origin\": \"local\"\r\n"
                "    }\r\n"
                "  },\r\n"
                "  \"done\": true\r\n"
                "}");
        response->addHeader("Location", "http://"+request->host()+"/api/files/local"+uploadedFilePath);
        request->send(response);

    }, [this](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        Serial.printf("FILE %s file %s\n", req->url().c_str(), filename.c_str() );
        if(index==0) {
            int pos = filename.lastIndexOf("/");
            filename = pos == -1 ? "/" + filename : filename.substring(pos);
        }
        handleUpload(req, filename, index, data, len, final);
    }, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) { // this can also be a json body

        Serial.printf("POST body %s\n", request->url().c_str() );
        if( request->hasHeader("Content-Type") ) Serial.println(request->getHeader("Content-Type")->value() );
/*
        static String content;

        if (index==0)
            content = "";
        for (int i = 0; i < len; ++i)
            content += (char)data[i];
        if (index+len == total) {
            Serial.printf("POST body %s\n", request->url().c_str() );
            if( request->hasHeader("Content-Type") ) Serial.println(request->getHeader("Content-Type")->value() );
            Serial.println(content);
            DynamicJsonDocument doc(1024);
            auto error = deserializeJson(doc, content);
            if (error)
                request->send(400, "text/plain", error.c_str());
            else {
                int responseCode = apiJobHandler(doc.as<JsonObject>());
                request->send(responseCode, "text/plain", "");
            }
            //request->send(200, "text/plain", "");
            content = "";
        }*/
    });


    server.on("/api/job", HTTP_GET, [this](AsyncWebServerRequest * request) {
        // http://docs.octoprint.org/en/master/api/job.html#retrieve-information-about-the-current-job
        Serial.println("GET /api/job");
        
        /*if (isPrinting) {
            printTime = (millis() - printStartTime) / 1000;
            printTimeLeft = (printCompletion > 0) ? printTime / printCompletion * (100 - printCompletion) : INT32_MAX;
        }*/
        Job *job = Job::getJob();
        if(job==nullptr) {//} || !job->isValid()) {
            request->send(500, "text/plain", "");
        }
        int32_t printTime = job->getPrintDuration()/1000, printTimeLeft = 0;
        request->send(200, "application/json", "{\r\n"
                "  \"job\": {\r\n"
                "    \"file\": {\r\n"
                "      \"name\": \"" + job->getFilename() + "\",\r\n"
                "      \"origin\": \"local\",\r\n"
                "      \"size\": " + String(job->getFileSize()) + ",\r\n"
                "      \"date\": \"2020-06-02-10:11:12\"\r\n"
                //"      \"date\": " + String(uploadedFileDate) + "\r\n"
                "    },\r\n"
                //"    \"estimatedPrintTime\": \"" + estimatedPrintTime + "\",\r\n"
                "    \"filament\": {\r\n"
                //"      \"length\": \"" + filementLength + "\",\r\n"
                //"      \"volume\": \"" + filementVolume + "\"\r\n"
                "    }\r\n"
                "  },\r\n"
                "  \"progress\": {\r\n"
                "    \"completion\": " + String(job->getPercentage() ) + ",\r\n"
                //"    \"filepos\": " + String(filePos) + ",\r\n"
                "    \"filepos\": 100,\r\n"
                "    \"printTime\": " + String(printTime) + ",\r\n"
                "    \"printTimeLeft\": " + String(printTimeLeft) + "\r\n"
                "  },\r\n"
                //"  \"state\": \"" + getState() + "\"\r\n"
                "  \"state\": \"Operational\"\r\n"
                "}");
    });

    server.on("/api/job", HTTP_POST, [](AsyncWebServerRequest *request) {
            // Job commands http://docs.octoprint.org/en/master/api/job.html#issue-a-job-command
            request->send(200, "text/plain", "");
        },
        [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
            request->send(400, "text/plain", "file not supported");
        },
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String content;

            if (index==0)
                content = "";
            for (int i = 0; i < len; ++i)
                content += (char)data[i];
            if (index+len == total) {
                Serial.println("POST /api/job");
                Serial.println(content);
                DynamicJsonDocument doc(1024);
                auto error = deserializeJson(doc, content);
                if (error)
                    request->send(400, "text/plain", error.c_str());
                else {
                    int responseCode = apiJobHandler(doc.as<JsonObject>());
                    request->send(responseCode, "text/plain", "");
                }
                request->send(200, "text/plain", "");
                content = "";
            }
        }
    );

    // this handles API key check from Cura
    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest * request) {
        // https://github.com/probonopd/WirelessPrinting/issues/30
        // https://github.com/probonopd/WirelessPrinting/issues/18#issuecomment-321927016
        request->send(200, "application/json", "{}");
    });


    server.on("/api/printer", HTTP_GET, [this](AsyncWebServerRequest * request) {
        Serial.print("GET "); Serial.println(request->url() );
        // https://docs.octoprint.org/en/master/api/printer.html#retrieve-the-current-printer-state
        //String readyState = stringify(printerConnected);
        String message = "{\r\n"
                "  \"state\": {\r\n"
                //"    \"text\": \"" + getState() + "\",\r\n"
                "    \"text\": \"Operational\",\r\n"
                "    \"flags\": {\r\n"
                //"      \"operational\": " + readyState + ",\r\n"
                "      \"operational\": true,\r\n"
                //"      \"paused\": " + stringify(printPause) + ",\r\n"
                "      \"paused\": false,\r\n"
                //"      \"printing\": " + stringify(isPrinting) + ",\r\n"
                "      \"printing\": false,\r\n"
                "      \"pausing\": false,\r\n"
                //"      \"cancelling\": " + stringify(cancelPrint) + ",\r\n"
                "      \"sdReady\": false,\r\n"
                "      \"error\": false,\r\n"
                //"      \"ready\": " + readyState + ",\r\n"
                "      \"ready\": true,\r\n"
                //"      \"closedOrError\": " + stringify(!printerConnected) + "\r\n"
                "      \"closedOrError\": false\r\n"
                "    }\r\n"
                "  },\r\n"
                "  \"temperature\": {\r\n";
        int fwExtruders = 1;
        for (int t = 0; t < fwExtruders; ++t) {
            message += "    \"tool" + String(t) + "\": {\r\n"
                    //"      \"actual\": " + toolTemperature[t].actual + ",\r\n"
                    //"      \"target\": " + toolTemperature[t].target + ",\r\n"
                    "      \"actual\": 25,\r\n"
                    "      \"target\": 50,\r\n"
                    "      \"offset\": 0\r\n"
                    "    },\r\n";
        }
        message += "    \"bed\": {\r\n"
                //"      \"actual\": " + bedTemperature.actual + ",\r\n"
                //"      \"target\": " + bedTemperature.target + ",\r\n"
                "      \"actual\": 25,\r\n"
                "      \"target\": 30,\r\n"
                "      \"offset\": 0\r\n"
                "    }\r\n"
                "  },\r\n"
                "  \"sd\": {\r\n"
                "    \"ready\": false\r\n"
                "  }\r\n"
                "}";
        request->send(200, "application/json", message);
    });

    // http://docs.octoprint.org/en/master/api/printer.html#send-an-arbitrary-command-to-the-printer
    server.on("/api/printer/command", HTTP_POST, [](AsyncWebServerRequest *request) {
            request->send(200, "text/plain", "");
        },
        [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
            request->send(400, "text/plain", "file not supported");
        },
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String content;
            
            if (!index)
                content = "";
            for (size_t i = 0; i < len; ++i)
                content += (char)data[i];
            if ( index+len == total) {
                /*DynamicJsonDocument doc(1024);
                auto error = deserializeJson(doc, content);
                if (error)
                    request->send(400, "text/plain", error.c_str());
                else {
                    JsonObject root = doc.as<JsonObject>();
                    const char* command = root["command"];
                    if (command != NULL)
                        commandQueue.push(command);
                    else {
                        JsonArray commands = root["commands"].as<JsonArray>();
                        for (JsonVariant command : commands)
                            commandQueue.push(String(command.as<String>()));
                    }
                    request->send(204, "text/plain", "");
                }*/
                Serial.println("POST /api/printer/command");
                Serial.println(content);
                request->send(204, "text/plain", "");
                content = "";
            }
            
        }
    );

}


int WebServer::apiJobHandler(JsonObject root) {
  const char* command = root["command"];
  Job *job = Job::getJob();

  if(job==nullptr)  { return 500; }

  if (command != NULL) {
    if (strcmp(command, "cancel") == 0) {
        if (!job->isRunning() )
            return 409;
        job->cancel();
    }
    else if (strcmp(command, "start") == 0) {
        if (job->isRunning() )
            return 409;
        if(!job->isValid() ) job->setFile(uploadedFilePath);
        job->start();
    }
    else if (strcmp(command, "restart") == 0) {
        //if (!printPause)
        return 409;
        //restartPrint = true;
    }
    else if (strcmp(command, "pause") == 0) {
        if (!job->isRunning() )
            return 409;
        const char* action = root["action"];
        if (action == NULL || strcmp(action, "toggle") == 0)
            job->setPaused(!job->isPaused() );
        else if (strcmp(action, "pause") == 0)
            job->pause();
        else if (strcmp(action, "resume") == 0)
            job->resume();
    }
  }

  return 204;
}

/** Retuns path in a form of /dir1/dir2 (leaading slash, no trailing slash). */
String WebServer::extractPath(String sdir) {
    sdir = sdir.substring(4); // cut "/fs/"
    if(sdir.length()>1 && sdir.endsWith("/") ) sdir=sdir.substring(0, sdir.length()-1); // remove trailing slash
    if(sdir.charAt(0) != '/') sdir = '/'+sdir;
    return sdir;
}

void WebServer::handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File file;

    if (index==0) { // first chunk
        Serial.printf("Uploading to file %s\n", filename.c_str() );
        uploadedFilePath = filename;

        if (uploadedFilePath.length() > 255)//storageFS.getMaxPathLength())
            uploadedFilePath = "/cached.gco";   // TODO maybe a different solution
        if(SD.exists(uploadedFilePath)) SD.remove(uploadedFilePath);
        file = SD.open(uploadedFilePath, "w"); // create or truncate file
    }

    //Serial.printf("uploading pos %d if size %d to %s\n", index, len, uploadedFullname.c_str() );
    file.write(data, len);

    if (final) { // last chunk
        Serial.printf("uploaded\n");
        uploadedFileSize = index + len;
        file.close();
    }
}

void WebServer::registerWebBrowser() {
        server.onNotFound([](AsyncWebServerRequest * request) {
        //telnetSend("404 | Page '" + request->url() + "' not found");
        Serial.printf("Sending 404 to %s\n", request->url().c_str() );
        request->send(404, "text/plain", "Page not found!");
    });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
        Serial.printf("Requested /, redirecting to /fs\n" );
        request->redirect("/fs/");
    });
    server.serveStatic("/fs/", SD, "/").setDefaultFile(""); // disable index.htm searching

    server.on("/fs", HTTP_GET, [](AsyncWebServerRequest * request) {
        String sdir = extractPath(request->url() );            
        
        Serial.println("listing dir "+sdir);
        File dir = SD.open(sdir);

        if(!dir || !dir.isDirectory()) { dir.close(); request->send(404, "text/plain", "No such file"); return; }

        String resp; resp.reserve(2048);
        resp += "<html><body>\n<h1>Listing of \""+sdir+"\"</h1>\n<form method='post' enctype='multipart/form-data'><input type='file' name='f'><input type='submit'></form>\n<ul>\n";

        if(sdir.length()>1) {
            int p=sdir.lastIndexOf('/'); 
            resp+="<li><a href=\"/fs/"+sdir.substring(0,p)+"\">../</a></li>\n";
        }
        File f;
        while( f = dir.openNextFile() ) {
            String fname = f.name(); 
            int p = fname.lastIndexOf('/'); fname = fname.substring(p+1);
            if(f.isDirectory())
                resp += "<li><a href=\"/fs"+String(f.name())+"/\">"+fname+"</a></li>\n";
            else 
                resp += "<li><a href=\"/fs"+String(f.name())+"\">"+fname+"</a> "+f.size()+"B</li>\n";
            f.close();
        }
        dir.close();
        resp += "\n</ul>\n</body></html>";
        request->send(200, "text/html", resp);
    });
    
    server.on("/fs", HTTP_POST, [](AsyncWebServerRequest * request) {
        request->send(201, "text/html", "created");
    }, [this](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if(index==0) {
            String sdir = req->url();
            sdir = extractPath(sdir);
            if(!sdir.endsWith("/") ) sdir += '/';
            filename = sdir + filename;
        }
        handleUpload(req, filename, index, data, len, final);
    } );
}
