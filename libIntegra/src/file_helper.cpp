/* libIntegra modular audio framework
 *
 * Copyright (C) 2007 Birmingham City University
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, 
 * USA.
 */


#include "platform_specifics.h"

#include <assert.h>
#include <filesystem>
#include <system_error>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "file_helper.h"
#include "file_io.h"
#include "api/trace.h"


namespace integra_internal
{
	string CFileHelper::extract_filename_from_path( const string &path )
	{
		size_t last_slash = path.find_last_of( '/' );
		size_t last_backslash = path.find_last_of( '\\' );

		size_t filename_start = ( last_slash == string::npos ) ? 0 : last_slash + 1;

		if( last_backslash != string::npos )
		{
			filename_start = MAX( filename_start, last_backslash + 1 );
		}

		return path.substr( filename_start );
	}


	string CFileHelper::extract_suffix_from_path( const string &path )
	{
		string filename = extract_filename_from_path( path );

		size_t last_dot = filename.find_last_of( '.' );

		if( last_dot == string::npos )
		{
			return "";
		}
		else
		{
			return filename.substr( last_dot + 1 );
		}
	}


	string CFileHelper::extract_directory_from_path( const string &path )
	{
		size_t last_slash = path.find_last_of( '/' );
		size_t last_backslash = path.find_last_of( '\\' );

		size_t directory_length = ( last_slash == string::npos ) ? 0 : last_slash + 1;

		if( last_backslash != string::npos )
		{
			directory_length = MAX( directory_length, last_backslash + 1 );
		}

		return path.substr( 0, directory_length );
	}


	string CFileHelper::extract_first_directory_from_path( const string &path )
	{
		size_t first_slash = path.find_first_of( '/' );
		size_t first_backslash = path.find_first_of( '\\' );

		if( first_slash == string::npos )
		{
			if( first_backslash == string::npos )
			{
				return string();
			}
			else
			{
				return path.substr( 0, first_backslash );
			}
		}
		else
		{
			if( first_backslash == string::npos )
			{
				return path.substr( 0, first_slash );
			}
			else
			{
				return path.substr( 0, MIN( first_slash, first_backslash ) );
			}
		}
	}


	namespace
	{
		/* libIntegra's strings are UTF-8 on every platform */
		std::filesystem::path to_fs_path( const string &path )
		{
			return std::filesystem::u8path( path );
		}
	}


	bool CFileHelper::is_directory( const string &directory_name )
	{
		std::error_code error;
		return std::filesystem::is_directory( to_fs_path( directory_name ), error );
	}


	void CFileHelper::delete_directory( const string &directory_name )
	{
		std::error_code error;
		std::filesystem::remove_all( to_fs_path( directory_name ), error );
		if( error )
		{
			INTEGRA_TRACE_ERROR << "Failed to remove directory " << directory_name << ": " << error.message();
		}
	}


	bool CFileHelper::make_directory( const string &directory_name )
	{
		std::error_code error;
		std::filesystem::create_directory( to_fs_path( directory_name ), error );
		return is_directory( directory_name );
	}


	std::vector<CFileHelper::directory_entry> CFileHelper::list_directory( const string &directory_name )
	{
		std::vector<directory_entry> entries;

		std::error_code error;
		std::filesystem::directory_iterator iterator( to_fs_path( directory_name ), error );
		if( error )
		{
			INTEGRA_TRACE_ERROR << "unable to open directory " << directory_name << ": " << error.message();
			return entries;
		}

		for( const auto &entry : iterator )
		{
			std::error_code type_error;
			entries.push_back( { entry.path().filename().u8string(), entry.is_directory( type_error ) } );
		}

		return entries;
	}


	bool CFileHelper::file_exists( const string &file_name )
	{
		FILE *file = fopen( file_name.c_str(), "rb" );
		if( file )
		{
			fclose( file );
			return true;
		}

		return false;
	}


	CError CFileHelper::delete_file( const string &file_name )
	{
		if( remove( file_name.c_str() ) != 0 )
		{
			INTEGRA_TRACE_ERROR << "Failed to remove file " << file_name;
			return CError::FAILED;
		}

		return CError::SUCCESS;
	}


	CError CFileHelper::copy_file( const string &source_path, const string &target_path )
	{
		CError error = CError::FAILED;

		FILE *source_file = fopen( source_path.c_str(), "rb" );
		if( !source_file )
		{
			INTEGRA_TRACE_ERROR << "failed to open: " << source_path;
			return CError::FAILED;
		}

		fseek( source_file, 0, SEEK_END );
		unsigned long bytes_to_copy = ftell( source_file );
		fseek( source_file, 0, SEEK_SET );

		FILE *target_file = fopen( target_path.c_str(), "wb" );
        unsigned char *copy_buffer = NULL;
        
		if( !target_file )
		{
			INTEGRA_TRACE_ERROR << "couldn't open for writing: " << target_path;
			goto CLEANUP;
		}

		copy_buffer = new unsigned char[ CFileIO::data_copy_buffer_size ];

		while( bytes_to_copy > 0 )
		{
			unsigned long bytes_read = fread( copy_buffer, 1, MIN( bytes_to_copy, CFileIO::data_copy_buffer_size ), source_file );
			if( bytes_read <= 0 )
			{
				INTEGRA_TRACE_ERROR << "error reading: " << source_path;
				goto CLEANUP;
			}

			fwrite( copy_buffer, 1, bytes_read, target_file );
			bytes_to_copy -= bytes_read;
		}

		error = CError::SUCCESS;

	CLEANUP:

		if( copy_buffer )	delete[] copy_buffer;
		if( target_file )	fclose( target_file );
		if( source_file )	fclose( source_file );

		return error;
	}


	void CFileHelper::construct_subdirectories( const string &root_directory, const string &relative_file_path )
	{
		string subdirectory = CFileHelper::extract_first_directory_from_path( relative_file_path );
		if( subdirectory.empty() )
		{
			return;
		}

		string root_and_subdirectory = root_directory + subdirectory + CFileIO::path_separator;

		make_directory( root_and_subdirectory );

		construct_subdirectories( root_and_subdirectory, relative_file_path.substr( subdirectory.length() + 1 ) );
	}


	string CFileHelper::ensure_filename_has_suffix( const string &filename, const string &suffix )
	{
		auto filename_length = filename.length();
		auto suffix_length = suffix.length();

		if( filename_length > suffix_length + 1 )
		{
			if( filename.substr( filename_length - suffix_length ) == suffix )
			{
				if( filename[ filename_length - suffix_length - 1 ] == '.' )
				{
					/* filename already has suffix */
					return filename;
				}
			}
		}

		return filename + "." + suffix;
	}
    
    string CFileHelper::get_resources_path()
    {
        /*
         Lookup order:
          1. INTEGRA_RESOURCES_PATH environment variable (lets hosts and tests relocate resources)
          2. platform default relative to the binary containing libIntegra
             (macOS framework: <binary dir>/Resources/, elsewhere: <binary dir>/resources/)
          3. the source-tree data directory baked in at build time (INTEGRA_RESOURCES_DIR)
        */
        const char *environment_path = getenv( "INTEGRA_RESOURCES_PATH" );
        if( environment_path && *environment_path )
        {
            string path( environment_path );
            if( path.back() != '/' && path.back() != '\\' ) path += "/";
            return path;
        }

        string path;

#ifndef _WIN32
        Dl_info info;
        if( dladdr( (void *)&CFileHelper::get_resources_path, &info ) && info.dli_fname )
        {
            #ifdef __APPLE__
                path = CFileHelper::extract_directory_from_path( info.dli_fname ) + "Resources/";
            #else
                path = CFileHelper::extract_directory_from_path( info.dli_fname ) + "resources/";
            #endif
        }
#else
        char module_path[ MAX_PATH ];
        HMODULE module = NULL;
        if( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR) &CFileHelper::get_resources_path, &module ) &&
            GetModuleFileNameA( module, module_path, MAX_PATH ) > 0 )
        {
            path = CFileHelper::extract_directory_from_path( module_path ) + "resources\\";
        }
#endif

#ifdef INTEGRA_RESOURCES_DIR
        if( path.empty() || !file_exists( path + "CollectionSchema.xsd" ) )
        {
            path = INTEGRA_RESOURCES_DIR;
        }
#endif

        return path;
    }

}
