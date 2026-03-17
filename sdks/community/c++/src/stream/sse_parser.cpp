#include "sse_parser.h"

namespace agui {

void SseParser::feed(const std::string& chunk) {
    m_buffer += chunk;
    processBuffer();
}

bool SseParser::hasEvent() const {
    return !m_eventStrings.empty();
}

std::string SseParser::nextEvent() {
    if (m_eventStrings.empty()) {
        return "";
    }
    
    std::string jsonStr = m_eventStrings.front();
    m_eventStrings.pop();
    return jsonStr;
}

void SseParser::clear() {
    m_buffer.clear();
    while (!m_eventStrings.empty()) {
        m_eventStrings.pop();
    }
    m_currentData.clear();
    m_lastError.clear();
}

void SseParser::flush() {
    // Force completion of current unfinished event
    if (!m_currentData.empty()) {
        finishEvent();
    }
}

std::string SseParser::getLastError() const {
    return m_lastError;
}

void SseParser::processBuffer() {
    size_t pos = 0;

    while ((pos = m_buffer.find('\n')) != std::string::npos) {
        std::string line = m_buffer.substr(0, pos);
        m_buffer = m_buffer.substr(pos + 1);

        // Remove trailing \r
        if (!line.empty() && line[line.length() - 1] == '\r') {
            line = line.substr(0, line.length() - 1);
        }

        // Empty line indicates end of event
        if (line.empty()) {
            finishEvent();
        } else {
            parseLine(line);
        }
    }
}

void SseParser::parseLine(const std::string& line) {
    // Ignore comment lines
    if (!line.empty() && line[0] == ':') {
        return;
    }

    // Find colon separator
    size_t colonPos = line.find(':');
    if (colonPos == std::string::npos) {
        return;
    }

    std::string field = line.substr(0, colonPos);
    std::string value = line.substr(colonPos + 1);

    // Remove leading space from value
    if (!value.empty() && value[0] == ' ') {
        value = value.substr(1);
    }

    // Only process data field, ignore event and id
    if (field == "data") {
        if (!m_currentData.empty()) {
            m_currentData += "\n";
        }
        m_currentData += value;
    }
    // Ignore other fields (event, id, retry, etc.)
}

void SseParser::finishEvent() {
    // Only create event when there is data
    if (!m_currentData.empty()) {
        // Store JSON string directly without parsing here
        m_eventStrings.push(m_currentData);
        m_lastError.clear();

        // Clear current event data
        m_currentData.clear();
    }
}

}  // namespace agui
