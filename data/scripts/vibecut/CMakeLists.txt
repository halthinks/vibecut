# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 vibecut contributors

INSTALL(FILES
    action_xclip.py
    audio_events_ast.py
    diarize_pyannote.py
    objects_detr.py
    ocr_tesseract.py
    semantic_minilm.py
    semantic_siglip.py
    requirements-audio-events.txt
    requirements-diarization.txt
    requirements-semantic.txt
    requirements-vision.txt
DESTINATION ${KDE_INSTALL_DATADIR}${DATA_INSTALL_PREFIX}/scripts/vibecut)
