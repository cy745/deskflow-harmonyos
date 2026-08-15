/*
 * Deskflow -- HarmonicOHOS Deskflow client -- background connection carrier
 * 以独立进程承载 Deskflow 连接原生 worker（processMode=1 NEW_PROCESS_ATTACH_TO_PARENT）。
 * 主界面隐藏到托盘时，主 Ability 进入后台，但承载进程保持 FOREGROUND，
 * 因此 TCP 连接与自动重连仍持续运行，无需重新打开主界面。
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

import { AbilityConstant, UIAbility, Want } from '@kit.AbilityKit';
import { commonEventManager } from '@kit.BasicServicesKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { display } from '@kit.ArkUI';
import deskflow from 'libentry.so';

const DOMAIN = 0x0000;
const TAG = 'DeskflowBG';

/** 广播给主窗口页面的事件名（与 Index.ets 保持一致） */
export const BGEVT_STATUS = 'deskflow.bg.status';
export const BGEVT_CLIP = 'deskflow.bg.clip';
export const BGEVT_STOP = 'deskflow.bg.stop';

export default class BackGroundAbility extends UIAbility {
  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    hilog.info(DOMAIN, TAG, 'BackGroundAbility onCreate');
    const host = want.parameters?.['host'] as string;

    if (!host) {
      hilog.info(DOMAIN, TAG, 'no host in want, running as pure carrier');
      this.wireEvents();
      return;
    }
    const port = Number(want.parameters?.['port'] ?? 24800);
    const screen = (want.parameters?.['screen'] as string) ?? 'HarmonyPC';
    const invert = want.parameters?.['invert'] === true;
    const autoReconnect = want.parameters?.['autoreconnect'] !== false;
    const clipboard = want.parameters?.['clipboard'] === true;
    const clipPush = want.parameters?.['clipPush'] as string;

    try {
      const d = display.getDefaultDisplaySync();
      const w = Math.round(d.width);
      const h = Math.round(d.height);
      deskflow.setInvertScroll(invert);
      deskflow.setAutoReconnect(autoReconnect);
      deskflow.setClipboardSync(clipboard);
      if (clipPush) {
        deskflow.pushClipboard(clipPush);
        hilog.info(DOMAIN, TAG, 'push clipboard len=%{public}d', clipPush.length);
      } else {
        deskflow.connectDeskflow(host, port, screen, w, h);
        hilog.info(DOMAIN, TAG, 'connect %{public}s:%{public}d', host, port);
      }
    } catch (e) {
      hilog.error(DOMAIN, TAG, 'connect/push failed: %{public}s', JSON.stringify(e));
    }

    this.wireEvents();
  }

  private wireEvents(): void {
    try {
      deskflow.onDeskflowStatus((event: deskflow.ValueChangeEvent) => {
        const msg: commonEventManager.CommonEventData = { event: BGEVT_STATUS, data: event.value };
        this.publish(msg);
      });
    } catch (e) {
      hilog.error(DOMAIN, TAG, 'status cb fail: %{public}s', JSON.stringify(e));
    }
    try {
      deskflow.onClipboardData((event: deskflow.ValueChangeEvent) => {
        const msg: commonEventManager.CommonEventData = { event: BGEVT_CLIP, data: event.value };
        this.publish(msg);
      });
    } catch (e) {
      hilog.error(DOMAIN, TAG, 'clip cb fail: %{public}s', JSON.stringify(e));
    }
    // 订阅「断开」事件：主窗口触发时停止后台承载
    try {
      const info: commonEventManager.CommonEventSubscribeInfo = { events: [BGEVT_STOP] };
      const sub = commonEventManager.createSubscriberSync(info);
      commonEventManager.subscribe(sub, (err, data) => {
        if (!err) {
          hilog.info(DOMAIN, TAG, 'stop request received');
          this.context.terminateSelf().catch((e: BusinessError) => {
            hilog.error(DOMAIN, TAG, 'terminateSelf fail: %{public}s', JSON.stringify(e));
          });
        }
      });
    } catch (e) {
      hilog.error(DOMAIN, TAG, 'stop sub fail: %{public}s', JSON.stringify(e));
    }
  }

  private publish(msg: commonEventManager.CommonEventData): void {
    try {
      const opts: commonEventManager.CommonEventPublishData = { data: msg.data };
      commonEventManager.publish(msg.event, opts, (err) => {
        if (err) {
          hilog.error(DOMAIN, TAG, 'publish %{public}s fail: %{public}s', msg.event, JSON.stringify(err));
        }
      });
    } catch (e) {
      hilog.error(DOMAIN, TAG, 'publish %{public}s error: %{public}s', msg.event, JSON.stringify(e));
    }
  }

  onDestroy(): void {
    hilog.info(DOMAIN, TAG, 'BackGroundAbility onDestroy, stopping client');
    try {
      deskflow.disconnectDeskflow();
    } catch (e) {
      hilog.error(DOMAIN, TAG, 'disconnect fail: %{public}s', JSON.stringify(e));
    }
  }
}