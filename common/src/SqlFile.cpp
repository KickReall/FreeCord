#include "SqlFile.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

std::string LoadSqlFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open SQL file: " + path + " (expected in the current working directory)");
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Убираем хвостовые переносы строк/пробелы — файл лишь пишет текст запроса,
    // редакторы обычно дописывают завершающий \n.
    while (!content.empty() && std::isspace(static_cast<unsigned char>(content.back()))) {
        content.pop_back();
    }

    if (content.empty()) {
        throw std::runtime_error("SQL file is empty: " + path);
    }

    return content;
}
