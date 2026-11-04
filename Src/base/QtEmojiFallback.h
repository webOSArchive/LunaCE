/* @@@LICENSE
*
*      Copyright (c) 2026 webOS Community Edition
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

#ifndef QT_EMOJI_FALLBACK_H
#define QT_EMOJI_FALLBACK_H

// [CE] Append the emoji font to the system UI's Qt font fallback list. Call once, before any
// text is drawn (Qt caches each family's fallback list the first time it loads that family).
void installQtEmojiFallback();

#endif
