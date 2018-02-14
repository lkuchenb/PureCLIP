if (SEQAN_ROOT)
	if (NOT EXISTS ${SEQAN_ROOT}/share/cmake/seqan/seqan-config.cmake)
		message ( FATAL_ERROR "SEQAN_ROOT was specified but '${SEQAN_ROOT}/share/cmake/seqan/seqan-config.cmake' does not exist." )
	endif()
else()
	set ( SEQAN_URL "https://github.com/seqan/seqan/releases/download/seqan-v2.4.0/seqan-library-2.4.0.zip")
	set ( SEQAN_MD5 "201d9455c3b391517466d82f78841810")
	set ( SEQAN_ZIP_OUT ${CMAKE_CURRENT_BINARY_DIR}/seqan-library-2.4.0.zip )
	set ( SEQAN_ROOT ${CMAKE_CURRENT_BINARY_DIR}/seqan-library-2.4.0 )

	if (NOT EXISTS ${SEQAN_ROOT}/share/cmake/seqan/seqan-config.cmake )
		# Download zip file
		message ("Downloading ${SEQAN_URL}")
		file (DOWNLOAD ${SEQAN_URL} ${SEQAN_ZIP_OUT}
			EXPECTED_MD5 ${SEQAN_MD5}
			SHOW_PROGRESS STATUS status)
		list ( GET status 0 ret )
		list ( GET status 0 str)
		if ( NOT ret EQUAL 0)
			message (FATAL_ERROR "Download failed")
		endif()
		# Unpack zip file
		message ("Unpacking ${SEQAN_ZIP_OUT}")
        execute_process(COMMAND cmake -E tar zxf ${SEQAN_ZIP_OUT} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
		# Remove zip file
		if ( EXISTS ${SEQAN_ZIP_OUT} )
			file (REMOVE ${SEQAN_ZIP_OUT})
		endif()
	endif()
	# Check if FindSeqAn.cmake can be found wher it should
	if (NOT EXISTS ${SEQAN_ROOT}/share/cmake/seqan/seqan-config.cmake)
		message (FATAL_ERROR "Failed to download and unpack '${SEQAN_URL}'")
	endif()
endif ()

LIST ( APPEND CMAKE_PREFIX_PATH ${SEQAN_ROOT}/share/cmake/seqan/ )
SET( SEQAN_INCLUDE_PATH ${SEQAN_ROOT}/include/ )
SET( SEQAN_DEFINITIONS "${SEQAN_DEFINITIONS} -DSEQAN_BGZF_NUM_THREADS=1" )

# Search for zlib as a dependency for SeqAn.
find_package (ZLIB REQUIRED)

# Load the SeqAn module and fail if not found.
find_package (SeqAn REQUIRED)
