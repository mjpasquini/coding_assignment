#include "veprom.h"
#include <stdio.h>
#include <iostream>

Veprom::eRetVal Veprom::create(size_t size, string & filename)
{
    // Choose a filename from "veprom_0.map" thru "veprom_255.map"
    FILE* fid = nullptr;
    for (int i = 0; i < N_VEPROMS_ALLOWED; i++)
    {
        char buf[SZ_FILENAME_BUF]; memset(buf, 0, sizeof(buf));
        sprintf(buf, "veprom_%i%s", i, FILENAME_EXT);
        fid = fopen(buf, "rb");
        if (fid == nullptr)
        {
            // Found free file - stop here
            filename = buf;
            fid = fopen(buf, "wb");
            break;
        }
        fclose(fid);
    }

    // Ensure free file was found
    if (fid == nullptr)
        return FilenamesExhausted;

    // Write binary buffer (zeros) to file
    uint8_t* data = (uint8_t*)malloc(size);
    if (data == nullptr)
        return MemoryAllocError;
    memset(data, 0, size);
    size_t retWrite = fwrite(data, sizeof(*data), size, fid);
    free(data);
    if (retWrite != size) 
        return FileCreationError;

    // Close file
    fclose(fid);
    return OK;
}

Veprom::eRetVal Veprom::load(string filename)
{
    // Make sure vEPROM file exists
    FILE* fidVeprom = fopen(filename.c_str(), "rb");
    if (fidVeprom == nullptr)
        return ContextNotFound;
    fclose(fidVeprom);

    // Save context
    FILE* fidContext = fopen(FILENAME_CONTEXT, "wb");
    if (fidVeprom == nullptr)
        return CannotOpenContext;
    size_t retWrite = fwrite(filename.c_str(), 1, filename.length(), fidContext);
    if (retWrite != filename.length())
        return CannotWriteContext;
    fclose(fidVeprom);

    return OK;
}

string Veprom::get_context()
{
    // Make sure vEPROM file exists
    FILE* fid = fopen(FILENAME_CONTEXT, "rb");
    if (fid == nullptr)
    {
        // not found
        return "";
    }   
    char buf[SZ_FILENAME_BUF]; memset(buf, 0, sizeof(buf));
    fread(buf, 1, sizeof(buf) - 1, fid);
    string filename = buf;
    fclose(fid);
    return filename;
}

bool Veprom::get_size(size_t* pSz)
{
    if (pSz == nullptr) return false;

    // Get loaded context
    string filename = get_context();
    if (filename == "") return false;
    
    // Open file and get its size
    FILE* fid = fopen(filename.c_str(), "rb");
    if (fid == NULL) return false;
    fseek(fid, 0, SEEK_END);
    *pSz = ftell(fid);
    rewind(fid);
    fclose(fid);
    return true;
}

Veprom::eRetVal Veprom::write_raw(size_t addr, uint8_t* data, size_t length)
{
    // Get loaded context
    string filename = get_context();
    if (filename == "")
        return ContextNotLoaded;
    
    // Get size of veprom
    size_t size = 0;
    if (!get_size(&size))
        return CannotGetSize;

    // Check capacity
    if (addr + length > size)
        return WriteOutOfBounds;

    // Read modify write
    FILE* fid = fopen(filename.c_str(), "rb");
    if (fid == NULL)
        return OpenFailedWriteRaw;
    uint8_t* buf = (uint8_t*)malloc(size);
    if (buf == 0)
        return MemoryAllocError;
    fread(buf, 1, size, fid);
    memcpy(buf + addr, data, length);
    fclose(fid);
    fid = fopen(filename.c_str(), "wb");
    fwrite(buf, 1, size, fid);
    fclose(fid);
    free(buf);
    return OK;
}

Veprom::eRetVal Veprom::read_raw(size_t addr, uint8_t* buf, size_t length)
{
    // Get loaded context
    string filename = get_context();
    if (filename == "")
        return ContextNotLoaded;

    // Check buffer
    if (buf == nullptr)
        return NullPtr;

    // Get veprom size
    size_t size = 0;
    if (!get_size(&size))
        return CannotGetSize;

    // Open file and get its size
    FILE* fid = fopen(filename.c_str(), "rb");
    if (fid == NULL)
        return OpenFailedReadRaw;
    
    // Check capacity
    if (addr + length > size)
        return ReadOutOfBounds;

    // Read to buffer
    fseek(fid, addr, SEEK_SET);
    fread(buf, 1, length, fid);
    fclose(fid);
    return OK;
}

size_t Veprom::get_free_pos()
{
    size_t pos = 0;
    sFileHeader hdr; memset(&hdr, 0, sizeof(hdr));

    while (true)
    {
        if (read_raw(pos, (uint8_t*)&hdr, sizeof(hdr)) != OK)
            return -1; // exhausted or error
        if (hdr.filename[0] == 0)
            return pos; // empty header --> DONE
        // move to next file position
        pos += sizeof(hdr) + hdr.length;
    }
}

Veprom::eRetVal Veprom::write(string filename, uint8_t* buf, size_t length)
{
    // Check filename (allow null terminator)
    if (filename.length() > SZ_FILENAME_BUF - 1)
        return FilenameTooLong;

    // Get first available free position
    size_t pos = get_free_pos();
    if (pos == -1)
        return WriteFileDriveFull;

    // Write header
    sFileHeader hdr; memset(&hdr, 0, sizeof(hdr));
    strcpy(hdr.filename, filename.c_str());
    hdr.length = length;
    eRetVal retHdr = write_raw(pos, (uint8_t*)&hdr, sizeof(hdr)); pos += sizeof(hdr);
    if (retHdr != OK)
        return retHdr;

    // Write data
    eRetVal retData = write_raw(pos, buf, length);
    if (retData != OK)
        return retData;
    return OK;
}

Veprom::eRetVal Veprom::list(vector<string> & list)
{
    list = {};
    size_t pos = 0;

    // get veprom size
    size_t size = 0;
    if (!get_size(&size))
        return CannotGetSize;

    // Loop and read headers
    sFileHeader hdr; memset(&hdr, 0, sizeof(hdr));
    while (true)
    {
        if (OK != read_raw(pos, (uint8_t*)&hdr, sizeof(hdr)))
            break; // nothing more to read, DONE
        if (hdr.filename[0] == 0)
            break; // filename empty, DONE
        pos += sizeof(hdr); // move along header
        pos += hdr.length; // move along length of file
        list.push_back(hdr.filename);
    }

    return OK;
}

Veprom::eRetVal Veprom::read(string filename, uint8_t** pBuf, size_t* pLen)
{
    if (pBuf == nullptr || pLen == nullptr)
        return NullPtr;

    size_t pos = 0;
    *pLen = 0;

    // get veprom size
    size_t size = 0;
    if (!get_size(&size))
        return CannotGetSize;

    // Find file
    sFileHeader hdr; memset(&hdr, 0, sizeof(hdr));
    while (true)
    {
        if (OK != read_raw(pos, (uint8_t*)&hdr, sizeof(hdr)))
            return FileNotFound; // not found
        pos += sizeof(hdr); // move along header
        if (strcmp(hdr.filename, filename.c_str()) == 0)
            break; // found!
        pos += hdr.length; // move along length of file
    }

    // malloc buffer...  NOTE: caller must free!
    *pBuf = (uint8_t*)malloc(hdr.length);
    if (*pBuf == nullptr)
        return MemoryAllocError;
    
    // read to buffer
    if (OK != read_raw(pos, *pBuf, hdr.length))
        return FilenameBufferInvalid;
    *pLen = hdr.length;
    return OK;
}