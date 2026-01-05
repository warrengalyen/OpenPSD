/**
 * @file psd_text_layer_parse.h
 * @brief Internal text layer parsing interface
 *
 * Private header for text layer parsing logic.
 * 
 * Copyright (c) 2025-2026 Warren Galyen
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction.
 * Part of the OpenPSD library.
 */

#ifndef PSD_TEXT_LAYER_PARSE_H
#define PSD_TEXT_LAYER_PARSE_H

#include <stdint.h>
#include "psd_text_layer.h"
#include "../include/openpsd/psd.h"
#include "../include/openpsd/psd_error.h"

/**
 * @brief Parse all text layers from additional layer info blocks
 *
 * Scans each layer's additional_data blocks for 'TySh' and 'tySh' keys
 * and populates doc->text_layers derived database.
 * Failures do not prevent PSD loading.
 *
 * @param doc Document with layers already parsed
 * @return PSD_OK on success, negative error code on failure
 */
PSD_INTERNAL psd_status_t psd_parse_text_layers(psd_document_t *doc);

/**
 * @brief Free all text layer data
 *
 * Called from psd_document_free to clean up derived text_layers database.
 *
 * @param doc Document to free text layers from
 */
PSD_INTERNAL void psd_free_text_layers(psd_document_t *doc);

#endif /* PSD_TEXT_LAYER_PARSE_H */
