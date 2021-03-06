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

#pragma once 

#include <sql/sql_base.hpp>
#include <mysql/mysql.h>

namespace sql {

class Mysql : public SqlBase {
public:
    Mysql();
    virtual ~Mysql();

    // Mysql Database initialization.
    //
    // Param db_port is database port.
    // Param db_host is database host.
    // Param db_user is database user.
    // Param db_pass is database user's password.
    // Param db_name is database name.
    bool Init(int db_port,
              const std::string& db_host,
              const std::string& db_user,
              const std::string& db_pass,
              const std::string& db_name) override;
    
    // Mysql Database execution statement operation, 
    // like insert, update,
    bool Exec(const std::string& sql) override;

private:
    bool query_row(const std::string& sql,
                   SQL_ROW& row) override;
    bool query_rows(const std::string& sql,
                    SQL_ROWS& rows) override;

private:
    MYSQL* mysql_;
};

}; // namespace sql