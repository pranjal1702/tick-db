#include "storage/parquet_reader.h"

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/exception.h>
#include <parquet/file_reader.h>

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace tick_db {

static std::string to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
    return str;
}

std::shared_ptr<arrow::Table> ParquetReader::read_table(const std::string& filepath) {
    auto file_result = arrow::io::ReadableFile::Open(filepath);
    if (!file_result.ok()) {
        std::cerr << "[ParquetReader] Failed to open file: " << filepath << " - " << file_result.status().ToString()
                  << "\n";
        return nullptr;
    }
    std::shared_ptr<arrow::io::ReadableFile> infile = *file_result;

    auto reader_result = parquet::arrow::OpenFile(infile, arrow::default_memory_pool());
    if (!reader_result.ok()) {
        std::cerr << "[ParquetReader] Failed to initialize Parquet reader: " << reader_result.status().ToString()
                  << "\n";
        return nullptr;
    }
    std::unique_ptr<parquet::arrow::FileReader> reader = std::move(*reader_result);

    auto table_result = reader->ReadTable();
    if (!table_result.ok()) {
        std::cerr << "[ParquetReader] Failed to read Parquet table: " << table_result.status().ToString() << "\n";
        return nullptr;
    }
    return *table_result;
}

std::string ParquetReader::read_embedded_index(const std::string& filepath) {
    auto file_result = arrow::io::ReadableFile::Open(filepath);
    if (!file_result.ok()) return "";
    std::shared_ptr<arrow::io::ReadableFile> infile = *file_result;

    auto reader_result = parquet::arrow::OpenFile(infile, arrow::default_memory_pool());
    if (!reader_result.ok()) return "";
    std::unique_ptr<parquet::arrow::FileReader> reader = std::move(*reader_result);

    auto metadata = reader->parquet_reader()->metadata();
    if (!metadata) return "";
    auto kv_meta = metadata->key_value_metadata();
    if (!kv_meta) return "";

    int idx = kv_meta->FindKey("tick_db.index.v1");
    if (idx >= 0) {
        return kv_meta->value(idx);
    }
    return "";
}

bool ParquetReader::read_raw_columns(const std::string& filepath, std::vector<RawColumn>& out_columns,
                                     ParquetFileInfo* out_info) {
    out_columns.clear();

    auto table = read_table(filepath);
    if (!table) {
        return false;
    }

    if (out_info) {
        out_info->filepath = filepath;
        out_info->num_rows = table->num_rows();
        out_info->num_columns = table->num_columns();
        out_info->num_row_groups = 1;
        out_info->column_names.clear();
        for (int i = 0; i < table->num_columns(); ++i) {
            out_info->column_names.push_back(table->field(i)->name());
        }
    }

    int num_cols = table->num_columns();
    int64_t num_rows = table->num_rows();

    for (int col_idx = 0; col_idx < num_cols; ++col_idx) {
        std::string col_name = table->field(col_idx)->name();
        std::shared_ptr<arrow::ChunkedArray> chunked_arr = table->column(col_idx);
        auto type_id = chunked_arr->type()->id();

        RawColumn raw_col;
        raw_col.name = col_name;

        if (type_id == arrow::Type::UINT64 || type_id == arrow::Type::UINT32 || type_id == arrow::Type::UINT16 ||
            type_id == arrow::Type::UINT8) {
            std::vector<uint64_t> vec;
            vec.reserve(num_rows);
            for (int chunk_idx = 0; chunk_idx < chunked_arr->num_chunks(); ++chunk_idx) {
                auto array = chunked_arr->chunk(chunk_idx);
                for (int64_t row = 0; row < array->length(); ++row) {
                    if (type_id == arrow::Type::UINT64) {
                        vec.push_back(std::static_pointer_cast<arrow::UInt64Array>(array)->Value(row));
                    } else if (type_id == arrow::Type::UINT32) {
                        vec.push_back(std::static_pointer_cast<arrow::UInt32Array>(array)->Value(row));
                    } else if (type_id == arrow::Type::UINT16) {
                        vec.push_back(std::static_pointer_cast<arrow::UInt16Array>(array)->Value(row));
                    } else if (type_id == arrow::Type::UINT8) {
                        vec.push_back(std::static_pointer_cast<arrow::UInt8Array>(array)->Value(row));
                    }
                }
            }
            raw_col.data = std::move(vec);
        } else if (type_id == arrow::Type::INT64 || type_id == arrow::Type::INT32 || type_id == arrow::Type::INT16 ||
                   type_id == arrow::Type::INT8 || type_id == arrow::Type::TIMESTAMP) {
            std::vector<int64_t> vec;
            vec.reserve(num_rows);
            for (int chunk_idx = 0; chunk_idx < chunked_arr->num_chunks(); ++chunk_idx) {
                auto array = chunked_arr->chunk(chunk_idx);
                for (int64_t row = 0; row < array->length(); ++row) {
                    if (type_id == arrow::Type::INT64) {
                        vec.push_back(std::static_pointer_cast<arrow::Int64Array>(array)->Value(row));
                    } else if (type_id == arrow::Type::TIMESTAMP) {
                        vec.push_back(std::static_pointer_cast<arrow::TimestampArray>(array)->Value(row));
                    } else if (type_id == arrow::Type::INT32) {
                        vec.push_back(std::static_pointer_cast<arrow::Int32Array>(array)->Value(row));
                    } else if (type_id == arrow::Type::INT16) {
                        vec.push_back(std::static_pointer_cast<arrow::Int16Array>(array)->Value(row));
                    } else if (type_id == arrow::Type::INT8) {
                        vec.push_back(std::static_pointer_cast<arrow::Int8Array>(array)->Value(row));
                    }
                }
            }
            raw_col.data = std::move(vec);
        } else {
            std::vector<uint8_t> vec;
            vec.reserve(num_rows);
            for (int chunk_idx = 0; chunk_idx < chunked_arr->num_chunks(); ++chunk_idx) {
                auto array = chunked_arr->chunk(chunk_idx);
                for (int64_t row = 0; row < array->length(); ++row) {
                    vec.push_back(0);
                }
            }
            raw_col.data = std::move(vec);
        }

        out_columns.push_back(std::move(raw_col));
    }

    return true;
}

bool ParquetReader::read_trades(const std::string& filepath, std::vector<Trade>& out_trades,
                                ParquetFileInfo* out_info) {
    out_trades.clear();

    auto table = read_table(filepath);
    if (!table) return false;

    from_columns(*table, out_trades);
    if (out_info) {
        out_info->filepath = filepath;
        out_info->num_rows = table->num_rows();
        out_info->num_columns = table->num_columns();
        out_info->num_row_groups = 1;
        out_info->column_names.clear();
        for (int i = 0; i < table->num_columns(); ++i) {
            out_info->column_names.push_back(table->field(i)->name());
        }
    }
    return true;
}

void ParquetReader::print_summary(const std::string& filepath) {
    ParquetFileInfo info;
    std::vector<RawColumn> cols;
    if (!read_raw_columns(filepath, cols, &info)) {
        std::cerr << "[ParquetReader] Failed to read summary for " << filepath << "\n";
        return;
    }

    std::cout << "==================================================\n";
    std::cout << " Parquet File Metadata Summary\n";
    std::cout << "==================================================\n";
    std::cout << " File Path:       " << info.filepath << "\n";
    std::cout << " Total Rows:      " << info.num_rows << "\n";
    std::cout << " Total Columns:   " << info.num_columns << "\n";
    std::cout << " Columns List:\n";
    for (size_t i = 0; i < info.column_names.size(); ++i) {
        std::cout << "   [" << i << "] " << info.column_names[i] << "\n";
    }
    std::cout << "==================================================\n";
}

}  // namespace tick_db
