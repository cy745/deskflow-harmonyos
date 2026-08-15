/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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
 */

export interface ValueChangeEvent {
  value: string;
}

export declare const onChange: (callback: (event: ValueChangeEvent) => void) => void;
export const addKeyEventInterceptor: () => string;
export const removeKeyEventInterceptor: () => string;
export const addEventInterceptor: () => string;
export const removeEventInterceptor: () => string;
export const requestInjection: () => string;
export const queryAuthorizedStatus: () => string;
export const cancelInjection: () => string;
export const injectMouseClickGlobal: (x: number, y: number) => string;
export const injectMouseTrail: (x0: number, y0: number, x1: number, y1: number, steps: number, intervalMs: number) => string;
export const injectKey: (keyCode: number) => string;
export const onDeskflowStatus: (callback: (event: ValueChangeEvent) => void) => void;
export const connectDeskflow: (host: string, port: number, name: string, screenW: number, screenH: number) => string;
export const disconnectDeskflow: () => string;
export const setInvertScroll: (invert: boolean) => string;
export const setAutoReconnect: (enable: boolean) => string;