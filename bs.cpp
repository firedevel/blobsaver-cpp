#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <unistd.h> // for getopt

using json = nlohmann::json;

// 默认值
#define DEFAULT_TSS_BIN = "tsschecker";
#define DEFAULT_XML_PATH = "/home/Blobs/blobsaver.xml";
#define DEFAULT_API_URL = "https://api.ipsw.me/v4/device";

// 用于存储设备信息的结构体
struct DeviceInfo {
    std::string name;
    std::string identifier;
    std::string ecid;
    std::string generator;
    std::string apnonce;
    std::string bb;
    std::string savePath;
};

// 用于存储固件信息的结构体
struct FirmwareInfo {
    std::string version;
    std::string buildid;
    std::string boardconfig;
    bool signedStatus;
};

// 用于存储libcurl的响应数据
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
    } catch (std::bad_alloc& e) {
        return 0;
    }
    return newLength;
}

// 使用XPath解析XML文件并提取设备信息
std::vector<DeviceInfo> parseXML(const std::string& filePath) {
    std::vector<DeviceInfo> devices;
    xmlDocPtr doc = xmlReadFile(filePath.c_str(), nullptr, 0);
    if (doc == nullptr) {
        std::cerr << "Failed to parse XML file." << std::endl;
        return devices;
    }

    // 创建XPath上下文
    xmlXPathContextPtr context = xmlXPathNewContext(doc);
    if (context == nullptr) {
        std::cerr << "Failed to create XPath context." << std::endl;
        xmlFreeDoc(doc);
        return devices;
    }

    // 查询设备节点
    xmlXPathObjectPtr result = xmlXPathEvalExpression((const xmlChar*)"//node[@name='blobsaver']/node[@name='app']/node[@name='Saved Devices']/node", context);
    if (result == nullptr || result->nodesetval == nullptr) {
        std::cerr << "No device nodes found." << std::endl;
        xmlXPathFreeContext(context);
        xmlFreeDoc(doc);
        return devices;
    }

    // 遍历设备节点
    for (int i = 0; i < result->nodesetval->nodeNr; ++i) {
        xmlNodePtr node = result->nodesetval->nodeTab[i];
        DeviceInfo device;
        device.name = (char*)xmlGetProp(node, (const xmlChar*)"name");

        // 提取设备属性
        xmlNodePtr child = node->children;
        while (child != nullptr) {
            if (child->type == XML_ELEMENT_NODE && xmlStrcmp(child->name, (const xmlChar*)"map") == 0) {
                xmlNodePtr entry = child->children;
                while (entry != nullptr) {
                    if (entry->type == XML_ELEMENT_NODE && xmlStrcmp(entry->name, (const xmlChar*)"entry") == 0) {
                        xmlChar* key = xmlGetProp(entry, (const xmlChar*)"key");
                        xmlChar* value = xmlGetProp(entry, (const xmlChar*)"value");
                        if (xmlStrcmp(key, (const xmlChar*)"Save Path") == 0) {
                            device.savePath = (char*)value;
                        } else if (xmlStrcmp(key, (const xmlChar*)"Device Identifier") == 0) {
                            device.identifier = (char*)value;
                        } else if (xmlStrcmp(key, (const xmlChar*)"ECID") == 0) {
                            device.ecid = (char*)value;
                        } else if (xmlStrcmp(key, (const xmlChar*)"Generator") == 0) {
                            device.generator = (char*)value;
                        } else if (xmlStrcmp(key, (const xmlChar*)"Apnonce") == 0) {
                            device.apnonce = (char*)value;
                        } else if (xmlStrcmp(key, (const xmlChar*)"BasebandSerialNumber") == 0) {
                            device.bb = (char*)value;
                        }
                        xmlFree(key);
                        xmlFree(value);
                    }
                    entry = entry->next;
                }
            }
            child = child->next;
        }

        devices.push_back(device);
    }

    // 释放资源
    xmlXPathFreeObject(result);
    xmlXPathFreeContext(context);
    xmlFreeDoc(doc);

    return devices;
}

// 发送HTTP请求并获取JSON响应
std::string fetchJSON(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return "";
        }
    }
    return readBuffer;
}

// 解析JSON响应并提取固件信息
std::vector<FirmwareInfo> parseJSON(const std::string& jsonStr) {
    std::vector<FirmwareInfo> firmwares;
    json j = json::parse(jsonStr);

    for (auto& firmware : j["firmwares"]) {
        FirmwareInfo info;
        info.version = firmware["version"];
        info.buildid = firmware["buildid"];
        info.boardconfig = j["boardconfig"];
        info.signedStatus = firmware["signed"];
        firmwares.push_back(info);
    }

    return firmwares;
}

// 检查文件是否存在
bool fileExists(const std::string& filePath) {
    struct stat buffer;
    return (stat(filePath.c_str(), &buffer) == 0);
}

// 生成并运行tsschecker命令
void runTSSChecker(const std::string& tssBin, const bool outputChanged, const std::string& outputPath, const DeviceInfo& device, const FirmwareInfo& firmware) {
    std::stringstream ss;
    ss << std::hex << device.ecid;
    unsigned long ecidDec;
    ss >> ecidDec;

    //转换小写版本号
    std::string boardconfig_lower;
std::transform(firmware.boardconfig.begin(), firmware.boardconfig.end(), std::back_inserter(boardconfig_lower),
               [](unsigned char c) { return std::tolower(c); });
               
    std::string fileName = std::to_string(ecidDec) + "_" + device.identifier + "_" + boardconfig_lower + "_" + firmware.version + "-" + firmware.buildid + "_" + device.apnonce + ".shsh2";
    
    //检查是否需要更改目录
    std::string filePath = outputChanged ? outputPath + "/" + fileName : device.savePath + "/" + fileName;
    
    std::cout << "\e[37m" << "File expected: " << fileName << "\e[39m";
    if (fileExists(filePath)) {
        std::cout << " Already exists! " << std::endl;
        return;
    }else{
      std::cout << std::endl;
    }

    std::string command = tssBin + " --device " + device.identifier + " --ecid " + device.ecid + " --apnonce " + device.apnonce + " --generator " + device.generator + " --boardconfig " + firmware.boardconfig + " --buildid " + firmware.buildid;

    // 检测是否提供了基带序列号
    bool isiPhone = (device.identifier.find("iPhone") != std::string::npos);
    if (device.bb.empty()) {
        command += " -b";
        if (isiPhone) {
            std::cout << "\e[33m" << "Waring: Not saving BaseBand Ticket for iPhone!" << "\e[39m" << std::endl;
        }
    } else {
        command += " --bbsnum " + device.bb;
        if (!isiPhone) {
            std::cout << "\e[32m" << "Saving BaseBand Ticket (non-iPhone)!" << "\e[39m" << std::endl;
        }
    }
    
    //检测是否指定outputPath
    if (outputChanged){
        command += " --save-path " + outputPath + " -s";
    } else {
        command += " --save-path " + device.savePath + " -s";//-s在后面更漂亮
    }
    
    std::cout << "\e[32m" << "Running command: " << command << "\e[39m" << std::endl;
    system(command.c_str());
}

// 打印帮助信息
void printHelp(const std::string& programName) {
    std::cout << "Usage: " << programName << " [OPTIONS]\n"
              << "Options:\n"
              << "  -t, --tss-bin PATH    Specify the path to tsschecker\n"
              << "  -x, --xml PATH        Specify the path to the XML file (default: " << DEFAULT_XML_PATH << ")\n"
              << "  -a, --api URL         Specify the API URL (default: " << DEFAULT_API_URL << ")\n"
              << "  -o, --output          Change output for every device\n"
              << "  -h, --help            Display this help message\n";
}

int main(int argc, char* argv[]) {
    std::string tssBin = DEFAULT_TSS_BIN;
    std::string xmlPath = DEFAULT_XML_PATH;
    std::string apiUrl = DEFAULT_API_URL;
    std::string outputPath;
    bool outputChanged = false;

    // 解析命令行参数
    int opt;
    while ((opt = getopt(argc, argv, "t:x:a:o:h")) != -1) {
        switch (opt) {
            case 't':
                tssBin = optarg;
                break;
            case 'x':
                xmlPath = optarg;
                break;
            case 'a':
                apiUrl = optarg;
                break;
            case 'h':
                printHelp(argv[0]);
                return 0;
            case 'o':
                outputPath = optarg;
                outputChanged = true;
                std::system(("mkdir -p " + outputPath).c_str()); //injection waring, cross platfrom unkind
                break;
            default:
                printHelp(argv[0]);
                return 1;
        }
    }

    //检查tsschecker
    struct stat sb;
    if (!(stat(tssBin.c_str(), &sb) == 0 && sb.st_mode & S_IXUSR)) {
    std::cout << "tsschecker unexecutable, please check!" << std::endl;
        return 1;
    }
    std::vector<DeviceInfo> devices = parseXML(xmlPath);
    std::cout << "Parsed " << devices.size() << " devices from XML." << std::endl;

    for (const auto& device : devices) {
        std::string url = apiUrl + "/" + device.identifier + "?type=ipsw";
        std::string jsonStr = fetchJSON(url);
        if (jsonStr.empty()) {
            std::cerr << "Failed to fetch JSON for device: " << device.identifier << std::endl;
            continue;
        }

        std::vector<FirmwareInfo> firmwares = parseJSON(jsonStr);
        std::cout << "Parsed " << firmwares.size() << " firmwares for device: " << device.identifier << std::endl;

        for (const auto& firmware : firmwares) {
            if (firmware.signedStatus) {
                runTSSChecker(tssBin, outputChanged, outputPath, device, firmware);
            }
        }
    }

    return 0;
}
