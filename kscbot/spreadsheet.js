import { fileURLToPath } from 'node:url';
import { readFileSync } from 'node:fs';

import { DateTime } from 'luxon';
import { google } from 'googleapis';

const self = fileURLToPath(import.meta.url);

export class SpreadsheetDataManager {
  constructor(credentialsPath, spreadsheetId, columns = []) {
    this.credentialsPath = credentialsPath;
    this.spreadsheetId = spreadsheetId;
    this.sheets = null;
    this.columns = columns;
  }

  async authenticate() {
    const credentials = JSON.parse(readFileSync(this.credentialsPath));
    const auth = new google.auth.GoogleAuth({
      credentials,
      scopes: ['https://www.googleapis.com/auth/spreadsheets'],
    });

    this.sheets = google.sheets({ version: 'v4', auth });
  }

  getSheetName(name = '') {
    const now = DateTime.now().setZone('Asia/Tokyo').toFormat('yyyy-MM');
    return (name !== '') ? name : now;
  }

  async sheetExists(sheetName) {
    try {
      const response = await this.sheets.spreadsheets.get({
        spreadsheetId: this.spreadsheetId,
      });

      return response.data.sheets.some(
        sheet => sheet.properties.title === sheetName
      );
    } catch (error) {
      console.error(error);
      return false;
    }
  }

  async createSheet(sheetName) {
    try {
      await this.sheets.spreadsheets.batchUpdate({
        spreadsheetId: this.spreadsheetId,
        resource: {
          requests: [{
            addSheet: {
              properties: {
                title: sheetName,
              },
            },
          }],
        },
      });

      await this.sheets.spreadsheets.values.update({
        spreadsheetId: this.spreadsheetId,
        range: `${sheetName}!1:1`, // TODO
        valueInputOption: 'RAW',
        resource: {
          values: [this.columns],
        },
      });
    } catch (error) {
      console.error(error);
    }
  }

  async updateColumns(sheetName, ...columns) {
    try {
      await this.sheets.spreadsheets.values.update({
        spreadsheetId: this.spreadsheetId,
        range: `${sheetName}!1:1`, // TODO
        valueInputOption: 'RAW',
        resource: {
          values: [columns],
        },
      });
    } catch (error) {
      console.error(error);
    }
  }

  async addData(...data) {
    try {
      const sheetName = this.getSheetName();

      if (!(await this.sheetExists(sheetName))) {
        await this.createSheet(sheetName);
      }

      console.log(data);

      await this.sheets.spreadsheets.values.append({
        spreadsheetId: this.spreadsheetId,
        range: `${sheetName}!A:Z`, // TODO
        valueInputOption: 'RAW',
        resource: { values: [data] },
      });
    } catch (error) {
      console.error(error);
    }
  }

  async readData(name = '') {
    try {
      const sheetName = this.getSheetName(name);

      if (!(await this.sheetExists(sheetName))) {
        return [];
      }

      const response = await this.sheets.spreadsheets.values.get({
        spreadsheetId: this.spreadsheetId,
        range: `${sheetName}!A2:D`,
      });

      const rows = response.data.values || [];
      return rows.map(row => ({
        temperature: parseFloat(row[0]),
        humidity: parseFloat(row[1]),
        timestamp: new Date(row[2]),
        tag: row[3] || '',
      }));
    } catch (error) {
      console.error(error);
      return [];
    }
  }

  async logData(getDataCallback) {
    try {
      const data = await getDataCallback();
      await this.addData(data.temperature, data.humidity, data.tag);
    } catch (error) {
      console.error(error);
    }
  }
}

async function main() {
  const manager = new SpreadsheetDataManager(
    './credentials.json',
    'SPREADSHEET_ID'
  );
  await manager.authenticate();
}

if (process.argv[1] === self) {
  console.log('single file execution');
  main().catch(console.error);
}
