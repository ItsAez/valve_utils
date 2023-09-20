#pragma once

#include "types.hpp"

#include <vector>
#include <string_view>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <optional>

#include <fmt/format.h>
#include <fmt/compile.h>

namespace valve
{
    namespace fs = std::filesystem;

#ifndef VALVE_UTIL_GUARD
    #define VALVE_UTIL_GUARD
    // Branchless ascii tolower
    constexpr char tolower( char c )
    {
        return c + ( c >= 'A' && c <= 'Z' ) * 32;
    }
#endif // VALVE_UTIL_GUARD

    template <typename T>
    class buffer_view
    {
    public:
        buffer_view( ) = default;
        buffer_view( T* data, usize size ) : m_data( data ), m_size( size ) {};

        bool empty( ) const { return !m_size; }
        usize size( ) const { return m_size; }
        usize size_bytes( ) const { return sizeof( T ) * m_size; }

        T* data( ) { return m_data; }
        const T* data( ) const { return m_data; }
        T* begin( ) { return m_data; }
        T* end( ) { return m_data + m_size; }
        const T* begin( ) const { return m_data; }
        const T* end( ) const { return m_data + m_size; }

        T& operator[]( usize idx ) { return m_data[ idx ]; }
        const T& operator[]( usize idx ) const { return m_data[ idx ]; }

        buffer_view<u8> as_bytes( ) { return buffer_view<u8>{ reinterpret_cast< u8* >( m_data ), size_bytes( ) }; }
        const buffer_view<u8> as_bytes( ) const { return buffer_view<u8>{ reinterpret_cast< u8* >( m_data ), size_bytes( ) }; }

    private:
        T* m_data = nullptr;
        usize m_size = 0;
    };

    struct vpk_entry_t
    {
        std::string_view m_pak_path;
        std::string_view m_filename;
        u32              m_archive_index;
        u32              m_data_offset;
        u32              m_data_size;
        buffer_view<u8>  m_preload_bytes;
        bool             m_preload_fullfile;

        std::optional<std::vector<u8>> get_data( ) const
        {
            std::vector<u8> buffer( m_preload_bytes.begin( ), m_preload_bytes.end( ) );

            if ( m_preload_fullfile )
                return std::make_optional( std::move( buffer ) );
            
            std::string_view archive_path = m_pak_path.substr( 0, m_pak_path.find_last_of( '.' ) - 3 );
            fs::path p = fs::u8path( fmt::format( FMT_COMPILE("{}{:03}.vpk"), archive_path, m_archive_index ) );

            std::ifstream in( p, std::ios::binary );

            if ( !in.good( ) )
                return std::nullopt;
            
            size_t old_size = buffer.size( );
            buffer.resize( old_size + m_data_size );

            in.seekg( m_data_offset );
            in.read( ( char* )( buffer.data( ) + old_size ), m_data_size );

            return std::make_optional( std::move( buffer ) );
        }
    };

    class vpk_file
    {
#pragma pack(push, 1)
        // https://developer.valvesoftware.com/wiki/.vpk
        struct vpk_header_v2_t
        {
            u32 Signature;  // 0x55aa1234
            u32 Version;    // 2

            // The size, in bytes, of the directory tree
            u32 TreeSize;

            // How many bytes of file content are stored in this VPK file (0 in CSGO)
            u32 FileDataSectionSize;

            // The size, in bytes, of the section containing MD5 checksums for external archive content
            u32 ArchiveMD5SectionSize;

            // The size, in bytes, of the section containing MD5 checksums for content in this file (should always be 48)
            u32 OtherMD5SectionSize;

            // The size, in bytes, of the section containing the public key and signature. This is either 0 (CSGO & The Ship) or 296 (HL2, HL2:DM, HL2:EP1, HL2:EP2, HL2:LC, TF2, DOD:S & CS:S)
            u32 SignatureSectionSize;
        };
        struct vpk_dir_entry_t
        {
            u32 CRC; // A 32bit CRC of the file's data.
            u16 PreloadBytes; // The number of bytes contained in the index file.

            // A zero based index of the archive this file's data is contained in.
            // If 0x7fff, the data follows the directory.
            u16 ArchiveIndex;

            // If ArchiveIndex is 0x7fff, the offset of the file data relative to the end of the directory (see the header for more details).
            // Otherwise, the offset of the data from the start of the specified archive.
            u32 EntryOffset;

            // If zero, the entire file is stored in the preload data.
            // Otherwise, the number of bytes stored starting at EntryOffset.
            u32 EntryLength;

            u16 Terminator; // 0xffff
        };
#pragma pack(pop)

        struct case_insensitive_hash
        {
            // FNV-1a 32bit hash
            std::size_t operator()( const std::string& s ) const
            {
                size_t hash = 0x811c9dc5;

                for ( i32 i = 0; i < s.size( ); i++ )
                {
                    char c = s[ i ];
                    hash ^= tolower( c );
                    hash *= 0x01000193;
                }

                return hash;
            }
        };
        struct case_insensitive_equal
        {
            bool operator()( const std::string& a, const std::string& b ) const
            {
                if ( a.size( ) != b.size( ) )
                    return false;

                for ( i32 i = 0; i < a.size( ); i++ )
                {
                    if ( tolower( a[ i ] ) != tolower( b[ i ] ) )
                        return false;
                }

                return true;
            }
        };

    public:
        using file_map_t = std::unordered_map<std::string, vpk_entry_t, case_insensitive_hash, case_insensitive_equal>;

        bool load( const fs::path& file )
        {
            std::ifstream in( file, std::ios::binary );

            if ( !in.good( ) )
                return false;

            size_t size = fs::file_size( file );

            vpk_header_v2_t header{};

            in.read( ( char* )&header, sizeof( vpk_header_v2_t ) );
            in.seekg( std::ios::beg );

            if ( header.Signature != 0x55aa1234 || header.Version != 2 )
                return false;

            m_pak_path = file.u8string( );

            m_buffer.resize( size );
            in.read( ( char* )m_buffer.data( ), size );
            in.close( );

            u8* tree_start = m_buffer.data( ) + sizeof( vpk_header_v2_t );
            u8* tree_end = tree_start + header.TreeSize;

            for ( u8* i = tree_start; i < tree_end; )
            {
                auto read_string = [ &i ]( ) -> std::string_view
                {
                    size_t length = std::strlen( reinterpret_cast< const char* >( i ) );
                    auto str_v = std::string_view{ reinterpret_cast< const char* >( i ), length };
                    i += length + 1;
                    return str_v;
                };

                std::string_view file_ext = read_string( );
                if ( file_ext.empty( ) )
                    continue;

                while ( true )
                {
                    std::string_view file_path = read_string( );
                    if ( file_path.empty( ) )
                        break;

                    while ( true )
                    {
                        std::string_view file_name = read_string( );
                        if ( file_name.empty( ) )
                            break;

                        vpk_dir_entry_t* vpk_entry = reinterpret_cast< vpk_dir_entry_t* >( i );
                        i += sizeof( vpk_dir_entry_t );

                        std::string path;
                        path.reserve( file_path.size( ) + file_name.size( ) + file_ext.size( ) + 1 );
                        path += file_path;
                        path += '/';
                        path += file_name;
                        path += '.';
                        path += file_ext;

                        auto [it, success] = m_files.try_emplace( std::move( path ), vpk_entry_t{} );
                        
                        if ( !success )
                            continue;

                        vpk_entry_t& map_entry       = it->second;
                        map_entry.m_pak_path         = m_pak_path;
                        map_entry.m_filename         = it->first;
                        map_entry.m_archive_index    = vpk_entry->ArchiveIndex;
                        map_entry.m_data_offset      = vpk_entry->EntryOffset;
                        map_entry.m_data_size        = vpk_entry->EntryLength;
                        map_entry.m_preload_fullfile = vpk_entry->EntryLength == 0;

                        if ( vpk_entry->PreloadBytes )
                        {
                            map_entry.m_preload_bytes = buffer_view<u8>{ i, vpk_entry->PreloadBytes };
                            i += vpk_entry->PreloadBytes;
                        }
                    }
                }
            }

            return true;
        }

        std::optional<const vpk_entry_t*> find( std::string_view file ) const
        {
            if ( auto result = m_files.find( file.data( ) ); result != m_files.end( ) )
                return std::make_optional( &result->second );

            return std::nullopt;
        }

    public:
        std::string     m_pak_path;
        std::vector<u8> m_buffer;
        file_map_t      m_files;
    };
}