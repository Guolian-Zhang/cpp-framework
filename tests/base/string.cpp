/*
 *
 * Copyright 2018 Guolian Zhang.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <iostream>
#include <string>
#include <base/base.h>

int main(int argc, char** argv) {
    std::cout << base::StringPrintf("test%d, test%s", 1, "2") << std::endl;
    std::cout << base::StringToInt("1") << std::endl;
    std::cout << base::StringToInt64("2") << std::endl;
    std::cout << base::StringToUInt("3") << std::endl;
    std::cout << base::StringToUInt64("4") << std::endl;
    std::cout << base::StringToFloat("5.0") << std::endl;
    std::cout << base::StringToDouble("6.0") << std::endl;
    std::cout << base::StringToBool("true") << std::endl;

    return 0;
}