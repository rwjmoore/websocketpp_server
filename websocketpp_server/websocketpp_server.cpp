#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <turbojpeg.h>
#include <opencv2/opencv.hpp>

#include <fstream>
#include <iostream>
#include <set>
#include <streambuf>
#include <string>

#include <cstddef>
/**
 * The telemetry server accepts connections and sends a message to
 * each client containing an integer count. 
 *
 *
 * This example also includes an example simple HTTP server that serves a web
 * dashboard displaying the count. This simple design is suitable for use
 * delivering a small number of files to a small number of clients. It is ideal
 * for cases like embedded dashboards that don't want the complexity of an extra
 * HTTP server to serve static files.
 *
 * This design *will* fall over under high traffic or DoS conditions. In such
 * cases you are much better off proxying to a real HTTP server for the http
 * requests.
 */

 //-----------------------------------------> Utilities <----------------------------------------
 //PURPOSE: To convert a string of hex values into a byte array 
std::vector<uint8_t> hexStringToByteArray(const std::string& hexString)
{
    std::vector<uint8_t> byteArray;

    // Loop through the hex string, two characters at a time 
    for (size_t i = 0; i < hexString.length(); i += 2) {
        // Extract two characters representing a byte 
        std::string byteString = hexString.substr(i, 2);

        // Convert the byte string to a uint8_t value 
        uint8_t byteValue = static_cast<uint8_t>(
            stoi(byteString, nullptr, 16));

        // Add the byte to the byte array 
        byteArray.push_back(byteValue);
    }

    return byteArray;
}

// Function to convert a string of hexadecimal values to a vector of bytes
std::vector<byte> hexStringToBytes(const std::string& hexString) {
    std::vector<byte> bytes;
    for (size_t i = 0; i < hexString.length(); i += 2) {
        std::string byteString = hexString.substr(i, 2);
        byte b = static_cast<byte>(std::stoul(byteString, nullptr, 16));
        bytes.push_back(b);
    }
    return bytes;
}

//-----------------------------------------> Utilities End <----------------------------------------


// -------------------------------------------------------> IMAGE DECODER  <------------------------------------------------------------------------------

class decoder {
    int dataByteSize = 1000000; ///////unsure what this should be, so set this very large 
    int dataID = 0;
    int dataLength = 0;
    int receivedLength = 0;
    int label = 1001;
    bool GZipMode = false;

public:
    void decodeFunc(uint8_t* im, int size) {
        // Create decoder
        tjhandle turbojpeg;
        turbojpeg = tjInitDecompress();
        if (turbojpeg == nullptr) {
            std::cerr << "Error initializing TurboJPEG" << std::endl;
            delete im;
            return;
        }

        int width, height, subsamp, colorspace;
        if (tjDecompressHeader3(turbojpeg, im, size, &width, &height, &subsamp, &colorspace) != 0) {
            std::cerr << "Error getting JPEG header" << std::endl;
            delete im;
            return;
        }
        std::cout << "Successfully read header with dimension (" << width << " x " << height << "), subsampling " << subsamp << ", and color space " << colorspace << ". Image size " << size << std::endl;

        // Allocate buffer for the decompressed image
        std::vector<unsigned char> imageBuffer(width * height * tjPixelSize[TJPF_BGRX]);

        // Decompress the JPEG stream
        if (tjDecompress2(turbojpeg, im, size, imageBuffer.data(), width, 0, height, TJPF_BGRX, TJFLAG_BOTTOMUP) != 0) {
            std::cerr << "Error decompressing JPEG" << std::endl;
            delete im;
            return;
        }

        // DO SOMETHING WITH THE DECODED IMAGE (in this case, show in openCV)
        cv::Mat decodedImage = cv::Mat(height,width, CV_8UC4, imageBuffer.data());
        cv::flip(decodedImage, decodedImage, 0); //vertically flip the image
        int targetWidth = 700;
        int targetHeight = 550;
        cv::resize(decodedImage, decodedImage, cv::Size(targetWidth, targetHeight), 0.0, 0.0, cv::INTER_CUBIC);

        cv::imshow("test",decodedImage);
        cv::waitKey(1);
        
        tjDestroy(turbojpeg);
        delete im;
    }

    uint16_t byte2uint16(uint8_t* d, int offset) {
        return d[offset] | (d[offset + 1] << 8);
    }
    uint32_t byte2uint32(uint8_t* d, int offset) {
        return d[offset] | (d[offset + 1] << 8) | (d[offset + 2] << 16) | (d[offset + 3] << 24);
    }

    void handlePackets(std::vector<byte> bytes) {
        // USING TURBOJPEG
        
        uint8_t* d = reinterpret_cast<uint8_t*>(bytes.data());

        if (bytes.size() <= 14) {
            std::cout << "Received too small chunk";
            return;
        }

        uint16_t _label = byte2uint16(d, 0);
        if (_label != label) {
            std::cout << "Incorrect label";
            return;
        }
        uint16_t _dataID = byte2uint16(d, 2);
        
        if (_dataID != dataID) receivedLength = 0;
        dataID = _dataID;
        uint32_t dataLength = byte2uint32(d, 4);
        int _offset = byte2uint32(d, 8);

        GZipMode = d[12] == 1;
        if (GZipMode) {
            std::cerr << "Should not be in GZip Mode" << std::endl;
            return;
        }

        int ColorReductionLevel = (int)d[13];
        int metaByteLength = 15;

        // New image incoming: If the size has changed, reset the buffer. Otherwise just overwrite it
        if (receivedLength == 0) {
            int imageSize = dataLength;
            if (imageSize > dataByteSize) {
                std::cout << "Image larger than allocated space. Reallocating..." << std::endl;
                if (dataByte != nullptr) {
                    delete[] dataByte;
                }
                dataByte = new uint8_t[imageSize];
            }
            std::cout << "Starting to read in new image with length " << dataLength << std::endl;
        }

        // Add new data to end of chunks received so far
        int chunkLength = bytes.size() - metaByteLength;
        if (_offset + chunkLength <= dataLength)
        {
            receivedLength += chunkLength;
            //std::cout << "ReceivedLength: " << receivedLength << std::endl;
            memcpy(dataByte + _offset, d + metaByteLength, chunkLength);
        }

        if (receivedLength == dataLength)
        {
            // Spawn a short-lived thread to decode the image
            // Create local copies of the relevant variables to work with in a thread without them changing
            uint8_t* im = new uint8_t[dataLength];
            memcpy(im, dataByte, dataLength);
            int size = dataLength;
            decodeFunc(im, size);
            //std::thread(&decoder::decodeFunc, this, im, size).detach(); // Don't wait for the thread
        }

    }

private:
    uint8_t* dataByte = new uint8_t[16384];
};
//--------------------------------------> Image Decoder End <-----------------------------------------------------

// ------------------------------------> Telemetry Server <-------------------------------------------------------
class telemetry_server {
public:
    typedef websocketpp::connection_hdl connection_hdl;
    typedef websocketpp::server<websocketpp::config::asio> server;
    typedef server::message_ptr message_ptr;

    decoder imDecoder; 

    telemetry_server() : m_count(0) {
        // set up access channels to only log interesting things
        m_endpoint.clear_access_channels(websocketpp::log::alevel::all);
        m_endpoint.set_access_channels(websocketpp::log::alevel::access_core);
        m_endpoint.set_access_channels(websocketpp::log::alevel::app);

        // Initialize the Asio transport policy
        m_endpoint.init_asio();

        // Bind the handlers we are using
        using websocketpp::lib::placeholders::_1;
        using websocketpp::lib::placeholders::_2;

        using websocketpp::lib::bind;
        m_endpoint.set_open_handler(bind(&telemetry_server::on_open, this, _1));
        m_endpoint.set_close_handler(bind(&telemetry_server::on_close, this, _1));
        m_endpoint.set_http_handler(bind(&telemetry_server::on_http, this, _1));
        m_endpoint.set_message_handler(bind(&telemetry_server::onMessage, this, _1,_2));
    }

    void run(std::string docroot, uint16_t port) {
        std::stringstream ss;
        ss << "Running telemetry server on port " << port << " using docroot=" << docroot;
        m_endpoint.get_alog().write(websocketpp::log::alevel::app, ss.str());

        m_docroot = docroot;

        // listen on specified port
        m_endpoint.listen(port);

        // Start the server accept loop
        m_endpoint.start_accept();

        // Set the initial timer to start telemetry
        set_timer();

        // Start the ASIO io_service run loop
        try {
            m_endpoint.run();
        }
        catch (websocketpp::exception const& e) {
            std::cout << e.what() << std::endl;
        }
    }

    void set_timer() {
        m_timer = m_endpoint.set_timer(
            1000,
            websocketpp::lib::bind(
                &telemetry_server::on_timer,
                this,
                websocketpp::lib::placeholders::_1
            )
        );
    }

    void on_timer(websocketpp::lib::error_code const& ec) {
        if (ec) {
            // there was an error, stop telemetry
            m_endpoint.get_alog().write(websocketpp::log::alevel::app,
                "Timer Error: " + ec.message());
            return;
        }

        std::stringstream val;
        val << "count is " << m_count++;

        // Broadcast count to all connections
        con_list::iterator it;
        for (it = m_connections.begin(); it != m_connections.end(); ++it) {
            m_endpoint.send(*it, val.str(), websocketpp::frame::opcode::text);
        }

        // set timer for next telemetry check
        set_timer();
    }

    void on_http(connection_hdl hdl) {
        // Upgrade our connection handle to a full connection_ptr
        server::connection_ptr con = m_endpoint.get_con_from_hdl(hdl);

        std::ifstream file;
        std::string filename = con->get_resource();
        std::string response;

        m_endpoint.get_alog().write(websocketpp::log::alevel::app,
            "http request1: " + filename);

        if (filename == "/") {
            filename = m_docroot + "index.html";
        }
        else {
            filename = m_docroot + filename.substr(1);
        }

        m_endpoint.get_alog().write(websocketpp::log::alevel::app,
            "http request2: " + filename);

        file.open(filename.c_str(), std::ios::in);
        if (!file) {
            // 404 error
            std::stringstream ss;

            ss << "<!doctype html><html><head>"
                << "<title>Error 404 (Resource not found)</title><body>"
                << "<h1>Error 404</h1>"
                << "<p>The requested URL " << filename << " was not found on this server.</p>"
                << "</body></head></html>";

            con->set_body(ss.str());
            con->set_status(websocketpp::http::status_code::not_found);
            return;
        }

        file.seekg(0, std::ios::end);
        response.reserve(file.tellg());
        file.seekg(0, std::ios::beg);

        response.assign((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        con->set_body(response);
        con->set_status(websocketpp::http::status_code::ok);
    }

    void on_open(connection_hdl hdl) {
        m_connections.insert(hdl);
    }

    void on_close(connection_hdl hdl) {
        m_connections.erase(hdl);
    }

    //Action Performed on messageRecieved
    void onMessage(connection_hdl hdl, message_ptr msg) {
        std::cout << "message Recieved\n";
        std::string message = websocketpp::utility::to_hex(msg->get_payload());
        std::vector<byte> bytes = hexStringToBytes(message);
        imDecoder.handlePackets(bytes);
        //std::cout << "message is: " << message;
    }

private:
    typedef std::set<connection_hdl, std::owner_less<connection_hdl>> con_list;

    server m_endpoint;
    con_list m_connections;
    server::timer_ptr m_timer;

    std::string m_docroot;

    // Telemetry data
    uint64_t m_count;
};




int main(int argc, char* argv[]) {
    telemetry_server s;

    std::string docroot;
    uint16_t port = 9003;

    if (argc == 1) {
        std::cout << "No arguments supplied ... Usage: telemetry_server [documentroot] [port] for commandline" << std::endl;
        
    }

    if (argc >= 2) {
        docroot = std::string(argv[1]);
    }

    if (argc >= 3) {
        int i = atoi(argv[2]);
        if (i <= 0 || i > 65535) {
            std::cout << "invalid port" << std::endl;
            return 1;
        }

        port = uint16_t(i);
    }

    s.run(docroot, port);
    return 0;
}