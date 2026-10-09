//
//  util.cpp
//  UnitTests
//
//  Created by Jamie Bullock on 14/08/2018.
//  Copyright © 2018 Birmingham Conservatoire. All rights reserved.
//

#include "util.hpp"

#include <filesystem>
#include <system_error>

int number_of_files_in_directory(const char *path)
{
    int count = 0;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(path, error))
    {
        if (entry.path().filename().string()[0] != '.')
        {
            ++count;
        }
    }
    return count;
}
