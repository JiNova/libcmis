/* libcmis
 * Version: MPL 1.1 / GPLv2+ / LGPLv2+
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License or as specified alternatively below. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * Major Contributor(s):
 * Copyright (C) 2011 SUSE <cbosdonnat@suse.com>
 *
 *
 * All Rights Reserved.
 *
 * For minor contributions see the git repository.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPLv2+"), or
 * the GNU Lesser General Public License Version 2 or later (the "LGPLv2+"),
 * in which case the provisions of the GPLv2+ or the LGPLv2+ are applicable
 * instead of those above.
 */
#include <sstream>

#include <curl/curl.h>

#include "atom-utils.hxx"
#include "xml-utils.hxx"

using namespace std;

namespace
{
    static const char chars64[]=
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    bool lcl_getBufValue( char encoded, int* value )
    {
        bool found = false;
        const char *i = chars64;
        while ( !found && *i )
        {
            if ( *i == encoded )
            {
                found = true;
                *value = ( i - chars64 );
            }
            ++i;
        }
        return found;
    }
}

namespace atom
{
    EncodedData::EncodedData( FILE* stream ) :
        m_stream( stream ),
        m_outStream( NULL ),
        m_encoding( ),
        m_decode( false ),
        m_pendingValue( 0 ),
        m_pendingRank( 0 ),
        m_missingBytes( 0 )
    {
    }
    
    EncodedData::EncodedData( ostream* stream ) :
        m_stream( NULL ),
        m_outStream( stream ),
        m_encoding( ),
        m_decode( false ),
        m_pendingValue( 0 ),
        m_pendingRank( 0 ),
        m_missingBytes( 0 )
    {
    }

    EncodedData::EncodedData( const EncodedData& copy ) :
        m_stream( copy.m_stream ),
        m_outStream( copy.m_outStream ),
        m_encoding( copy.m_encoding ),
        m_decode( copy.m_decode ),
        m_pendingValue( copy.m_pendingValue ),
        m_pendingRank( copy.m_pendingRank ),
        m_missingBytes( copy.m_missingBytes )
    {
    }

    const EncodedData& EncodedData::operator=( const EncodedData& copy )
    {
        m_stream = copy.m_stream;
        m_outStream = copy.m_outStream;
        m_encoding = copy.m_encoding;
        m_decode = copy.m_decode;
        m_pendingValue = copy.m_pendingValue;
        m_pendingRank = copy.m_pendingRank;
        m_missingBytes = copy.m_missingBytes;
        return *this;
    }

    void EncodedData::write( void* buf, size_t size, size_t nmemb )
    {
        if ( m_stream )
            fwrite( buf, size, nmemb, m_stream );
        else if ( m_outStream )
            m_outStream->write( ( const char* )buf, size * nmemb );
    }

    void EncodedData::decode( void* buf, size_t size, size_t nmemb )
    {
        m_decode = true;
        if ( 0 == m_encoding.compare( "base64" ) )
        {
            decodeBase64( ( const char* )buf, size * nmemb );
        }
        else
            write( buf, size, nmemb );
    }

    void EncodedData::encode( void* buf, size_t size, size_t nmemb )
    {
        m_decode = false;
        if ( 0 == m_encoding.compare( "base64" ) )
        {
            encodeBase64( ( const char* )buf, size * nmemb );
        }
        else
            write( buf, size, nmemb );
    }

    void EncodedData::finish( )
    {
        // Flushes the last bytes in base64 encoding / decoding if any
        if ( 0 == m_encoding.compare( "base64" ) )
        {
            if ( m_decode && ( m_pendingValue != 0 || m_pendingRank != 0 || m_missingBytes != 0 ) )
            {
                int missingBytes = m_missingBytes;
                if ( 0 == m_missingBytes )
                    missingBytes = 4 - m_pendingRank;

                char decoded[3];
                decoded[0] = ( m_pendingValue & 0xFF0000 ) >> 16;
                decoded[1] = ( m_pendingValue & 0xFF00 ) >> 8;
                decoded[2] = ( m_pendingValue & 0xFF );

                write( decoded, 1, 3 - missingBytes );

                m_pendingRank = 0;
                m_pendingValue = 0;
                m_missingBytes = 0;
            }
            else if ( !m_decode && ( m_pendingValue != 0 || m_pendingRank != 0 ) )
            {
                // Missing bytes should be zeroed: no need to do it
                char encoded[4];
                encoded[0] = chars64[ ( m_pendingValue & 0xFC0000 ) >> 18 ];
                encoded[1] = chars64[ ( m_pendingValue & 0x03F000 ) >> 12 ];
                encoded[2] = chars64[ ( m_pendingValue & 0x000FC0 ) >> 6  ];
                encoded[3] = chars64[ ( m_pendingValue & 0x00003F )       ];

                // Output the padding
                int nEquals = 3 - m_pendingRank;
                for ( int i = 0; i < nEquals; ++i )
                    encoded[ 3 - i ] =  '=';

                write( encoded, 1, 4 );

                m_pendingRank = 0;
                m_pendingValue = 0;
            }
        }
    }

    void EncodedData::decodeBase64( const char* buf, size_t len )
    {
        unsigned long blockValue = m_pendingValue;
        int byteRank = m_pendingRank;
        int missingBytes = m_missingBytes;

        size_t i = 0;
        while ( i < len )
        {
            int value = 0;
            if ( lcl_getBufValue( buf[i], &value ) )
            {
                blockValue += value << ( ( 3 - byteRank ) * 6 );
                ++byteRank;
            }
            else if ( buf[i] == '=' )
            {
                ++missingBytes;
                ++byteRank;
            }

            // Reached the end of a block, decode it
            if ( byteRank >= 4 )
            {
                char decoded[3];
                decoded[0] = ( blockValue & 0xFF0000 ) >> 16;
                decoded[1] = ( blockValue & 0xFF00 ) >> 8;
                decoded[2] = ( blockValue & 0xFF );

                write( decoded, 1, 3 - missingBytes );

                byteRank = 0;
                blockValue = 0;
                missingBytes = 0;
            }
            ++i;
        }

        // Store the values if the last block is incomplete: they may come later
        m_pendingValue = blockValue;
        m_pendingRank = byteRank;
        m_missingBytes = missingBytes;
    }

    void EncodedData::encodeBase64( const char* buf, size_t len )
    {
        unsigned long blockValue = m_pendingValue;
        int byteRank = m_pendingRank;

        size_t i = 0;
        while ( i < len )
        {
            // Cast the char to an unsigned char or we'll shift negative values
            blockValue += static_cast< unsigned char >( buf[i] ) << ( 2 - byteRank ) * 8;
            ++byteRank;

            // Reached the end of a block, encode it
            if ( byteRank >= 3 )
            {
                char encoded[4];
                encoded[0] = chars64[ ( blockValue & 0xFC0000 ) >> 18 ];
                encoded[1] = chars64[ ( blockValue & 0x03F000 ) >> 12 ];
                encoded[2] = chars64[ ( blockValue & 0x000FC0 ) >> 6  ];
                encoded[3] = chars64[ ( blockValue & 0x00003F )       ];

                write( encoded, 1, 4 );

                byteRank = 0;
                blockValue = 0;
            }
            ++i;
        }

        // Store the values if the last block is incomplete: they may come later
        m_pendingValue = blockValue;
        m_pendingRank = byteRank;
    }

    void registerNamespaces( xmlXPathContextPtr xpathCtx )
    {
        xmlXPathRegisterNs( xpathCtx, BAD_CAST( "app" ),  NS_APP_URL );
        xmlXPathRegisterNs( xpathCtx, BAD_CAST( "atom" ),  NS_ATOM_URL );
        xmlXPathRegisterNs( xpathCtx, BAD_CAST( "cmis" ),  NS_CMIS_URL );
        xmlXPathRegisterNs( xpathCtx, BAD_CAST( "cmisra" ),  NS_CMISRA_URL );
        xmlXPathRegisterNs( xpathCtx, BAD_CAST( "xsi" ), BAD_CAST( "http://www.w3.org/2001/XMLSchema-instance" ) );
        xmlXPathRegisterNs( xpathCtx, BAD_CAST( "ns3" ), BAD_CAST( "http://docs.oasis-open.org/ns/cmis/messaging/200908/" ) );
        xmlXPathRegisterNs( xpathCtx, BAD_CAST( "type" ), BAD_CAST( "cmis:cmisTypeDocumentDefinitionType" ) );
    }
    
    string getXPathValue( xmlXPathContextPtr xpathCtx, string req )
    {
        string value;
        xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression( BAD_CAST( req.c_str() ), xpathCtx );
        if ( xpathObj && xpathObj->nodesetval && xpathObj->nodesetval->nodeNr > 0 )
        {
            xmlChar* pContent = xmlNodeGetContent( xpathObj->nodesetval->nodeTab[0] );
            value = string( ( char* )pContent );
            xmlFree( pContent );
        }
        xmlXPathFreeObject( xpathObj );

        return value;
    }

    xmlDocPtr wrapInDoc( xmlNodePtr entryNd )
    {
        xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
        xmlNodePtr entryCopy = xmlCopyNode( entryNd, 1 );

        xmlDocSetRootElement( doc, entryCopy );
        return doc;
    }
}
