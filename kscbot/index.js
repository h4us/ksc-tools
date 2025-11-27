/**
 * --
 */

// Native
import { exec } from 'node:child_process';

// Packages
import { DateTime } from 'luxon';
import { Server as OSCServer, Client as OSCClient } from 'node-osc';
import { Client, GatewayIntentBits } from 'discord.js';
import { TwitterApi } from "twitter-api-v2";
import { libcamera } from 'libcamera';

import 'dotenv/config';

import { deviceMap } from './devicemap.js';
import { SpreadsheetDataManager } from './spreadsheet.js';

const twitterClient = new TwitterApi({
  appKey: process.env.X_APP_KEY,
  appSecret: process.env.X_APP_SECRET,
  accessToken: process.env.X_APP_ACCESS_TOKEN,
  accessSecret: process.env.X_APP_ACCESS_SECRET,
});

const discordChannelCamera = process.env.DISCORD_CHANNEL_CAMERA;

const updateTrig = process.env.UPDATE_TRIG ? process.env.UPDATE_TRIG : 'internal';
const botName = process.env.BOT_NAME ? process.env.BOT_NAME: 'kscbot';
const regularTaskInterval = process.env.REGULAR_TASK_INTERVAL ? parseInt(process.env.REGULAR_TASK_INTERVAL) : 10;

let discordConnected = false;
let lastTrigTime = -1;
let wanErrState = 0;
let devices = [];

console.info(`<${botName}> started at ${DateTime.now().setZone('Asia/Tokyo')}`);

const sleep = (ms) => new Promise(resolve => setTimeout(resolve, ms));

const loginDiscord = async (client, token) => {
  await client.login(token).catch((err) => {
    console.error(err);
    console.error('..retry after 3 seconds');
    setTimeout(_ => loginDiscord(client, token), 3000);
  });
};

class SensorDevice {
  tag = '';
  props = {
    label: '_NO_LABEL_',
    localIP: '',
    connectionType: 'osc',
    humidity: 0, temperature: 0
  };

  constructor (tag = '', defaultProps) {
    this.tag = tag;
    Object.assign(this.props, defaultProps);
  }

  getProp(p) {
    return this.props[p];
  }

  setProp(p, v) {
    this.props[p] = v;
    return this.props[p];
  }
};

const main = async () => {
  await sleep(3 * 1000);

  const client = new Client(
    {
      intents: [
        GatewayIntentBits.Guilds,
        GatewayIntentBits.GuildMessages,
        GatewayIntentBits.MessageContent
      ]
    }
  );

  client.on('clientReady', (client) => {
    discordConnected = true;
    console.info('..Discord interface activated');

    for (const el of client.channels.cache) { console.info(el[1].name); }
  });

  await loginDiscord(client, process.env.DISCORD_TOKEN);

  const oscServer = new OSCServer(12000, '0.0.0.0');

  devices = deviceMap.map((el) => {
    const { tag, ...props } = el;
    return new SensorDevice(tag, props);
  });
  console.info('<created devices>', devices);

  let colData = ['timestamp'];
  for (const dev of devices) {
    colData = [
      ...colData,
      `[${dev.getProp('label')}] temperature(\u2103)`, `[${dev.getProp('label')}] humidity(%)`
    ];
  }
  const spreadsheets = new SpreadsheetDataManager(
    './.credentials/gcp.json',
    process.env.GOOGLE_SPREADSHEET_ID,
    colData
  );
  await spreadsheets.authenticate().catch((err) => console.error(err));
  console.info('..Google Spreadsheet interface activated');

  oscServer.on('message', async (e) => {
    const now = DateTime.now().setZone('Asia/Tokyo');

    /*
    // TODO: cleanup
    if (e[0] === '/reply' || e[0] === '/local') {
      if (now.minute % 10 == 0 && lastTrigTime != now.minute && discordConnected) {
        lastTrigTime = now.minute;
        if (now.hour % 2 == 0 && now.minute == 0 && !paramLock) {

        }
      }

      if (!paramLock && !isCooling) {
        if (now.minute % 2 == 0 && now.minute != 0 && pumpActive) {
          pumpActive = false;
          action = true;
        }

        if (action && subsysAsRemote.length > 0) {
          subsysAsRemote.forEach((el) => {
            const replyClient = new OSCClient(el, 12000);
            replyClient.send('/cmd', pumpActive ? 1 : 0);
          });
        }
      }
    }
    */

    if (e[0] !== '/trig') {
      /*
       * utils, TBA more functions
       */
      if (/^\/cmd(\/.+)*$/.test(e[0])) {
        if (e[0] === '/cmd/reboot') {
          exec('sudo systemctl reboot', (err) => console.error(err));
        } else if (e[0] == '/cmd/capture') {
          if (parseInt(process.env.WITH_RPI_CAMERA) == 1) {
            let targetChannel = false;
            await libcamera.jpeg({ config: { output: 'capture.jpg', width: 1920, rotation: 180 } });
            for (const el of client.channels.cache) {
              if (el[1].name == discordChannelCamera) {
                targetChannel = client.channels.cache.get(el[1].id);
              }
            }
            if (targetChannel) { await targetChannel.send({ files: ['capture.jpg'] }); }
          }
        } else if (e[0] === '/cmd/sheet') {
          let colData = ['timestamp'];
          for (const dev of devices) {
            colData = [
              ...colData,
              `[${dev.getProp('label')}] temperature(\u2103)`, `[${dev.getProp('label')}] humidity(%)`
            ];
          }
          await spreadsheets.updateColumns(spreadsheets.getSheetName(), ...colData);
        }
      }
      /*
       * devices feedback
       */
      else {
        const [oscAddr, ...oscArgs] = e;
        const dIdx = devices.findIndex((el) => {
          const re = new RegExp(`^\\${el.tag}(\/.+)*$`);
          return re.test(oscAddr);
        });

        if (dIdx > -1) {
          const dev = devices[dIdx];

          if (/.*\/ping$/.test(oscAddr)) {
            dev.setProp('localIP', oscArgs[0]);
            const replyClient = new OSCClient(dev.getProp('localIP'), 12000);
            replyClient.send('/sync', now.hour, now.minute, now.second);
          } else {
            dev.setProp('humidity', oscArgs[0].toFixed(2));
            dev.setProp('temperature', oscArgs[1].toFixed(2));
          }

          // console.info('<update device-status> ', oscAddr, dev);
        }
      }
    } else {
      /*
       * regularly tasks here
       * when <updateTrig> sets to 'internal', they will run regularly
       */
      if (now.minute % regularTaskInterval == 0 && lastTrigTime != now.minute && discordConnected) {
        lastTrigTime = now.minute;

        // console.info('<process> start publishing', now);
        let bundleData = [];

        for (const dev of devices) {
          let targetChannel = false;

          for (const el of client.channels.cache) {
            if (el[1].name == dev.getProp('discordChannel')) {
              targetChannel = client.channels.cache.get(el[1].id);
            }
          }

          if (targetChannel) {
            try {
              let nmsg = `[${now}][${dev.getProp('label')}] humidity: ${dev.getProp('humidity')}%, temperature: ${dev.getProp('temperature')}\u2103`;
              if (now.hour % 2 == 0 && now.minute == 0 && dev.getProp('hasPump')) nmsg += ' <Pump active>';
              // console.info('<process> publishing to Discord..', nmsg);
              await targetChannel.send(nmsg).then(_ => console.info(nmsg));
            } catch (err) {
              console.error('network error', err);
              wanErrState++;
            }
          }

          bundleData = [
            ...bundleData,
            parseFloat(dev.getProp('temperature')), parseFloat(dev.getProp('humidity'))
          ];
        }

        // TODO: more configurable schedule
        if (now.minute == 0 && bundleData.length > 0) {
          bundleData = [DateTime.now().setZone('Asia/Tokyo').toISO(), ...bundleData];
          await spreadsheets.addData(...bundleData);
        }

        // TODO: more configurable schedule
        if (now.minute == 0 && now.hour == 7) {
          try {
            const msgs = devices.map((dev) => `[${dev.getProp('label')}] humidity: ${dev.getProp('humidity')}%, temperature: ${dev.getProp('temperature')}\u2103`);
            const msgToX = msgs.join('\r\n');
            // console.info('<process> publishing to X..', msgs, msgToX);
            await twitterClient.v2.tweet(msgToX);
          } catch (err) {
            console.error('network error', err);
            wanErrState++;
          }
        }

        if (parseInt(process.env.WITH_RPI_CAMERA) == 1) {
          // TODO: more configurable schedule
          if (
            (now.hour == 11 && now.minute == 0)
            || (now.hour == 17 && now.minute == 0)
            || (now.hour == 7 && now.minute == 0)
            || (now.hour == 19 && now.minute == 0)
          ) {
            try {
              let targetChannel = false;
              await libcamera.jpeg({ config: { output: 'capture.jpg', width: 1920, rotation: 180 } });
              for (const el of client.channels.cache) {
                if (el[1].name == discordChannelCamera) {
                  targetChannel = client.channels.cache.get(el[1].id);
                }
              }
              if (targetChannel) { await targetChannel.send({ files: ['capture.jpg'] }); }
            } catch (err) {
              console.error('network error', err);
              wanErrState++;
            }
          }
        }

        if (wanErrState > 2) {
          console.error(`[${now}] network error is detected multiple times. will be trying to reboot system soon..`);
          setTimeout(_ => exec('sudo systemctl reboot', (err) => console.error(err)), 4000);
        }
      }
    }
  });

  // TODO: now, there is no other option
  if (updateTrig == 'internal') {
    console.info('<config> run with internal trigger mode');
    const oscClient = new OSCClient('0.0.0.0', 12000);
    setInterval(() => {
      oscClient.send('/trig');
    }, 1000);
  }
};

main();
