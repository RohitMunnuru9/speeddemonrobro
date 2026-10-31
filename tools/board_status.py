"""Read the public board and rank registered teams by their best three tracks."""
import json
from pathlib import Path
import time
import urllib.request

url = 'https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json'
for attempt in range(3):
    try:
        with urllib.request.urlopen(f'{url}?check={time.time_ns()}', timeout=30) as response:
            feed = json.load(response)
        break
    except (json.JSONDecodeError, OSError):
        if attempt == 2:
            raise
        time.sleep(1)
Path('.results/board.json').write_text(json.dumps(feed, indent=2) + '\n')
ranked = []
for team in feed['teams']:
    points = sorted((t['best']['points'] for t in team['tracks'].values()
                     if t.get('registered') and (t.get('best') or {}).get('ok')), reverse=True)
    if len(points) >= 3:
        ranked.append(((points[0] * points[1] * points[2]) ** (1 / 3), team))
ranked.sort(key=lambda row: row[0], reverse=True)
print('Board generated:', feed['generated_at'])
for rank, (score, team) in enumerate(ranked, 1):
    if rank <= 22 or team['team'] == 'vayu':
        print(rank, team['team'], round(score, 4),
              {k: (v.get('best') or {}).get('T_s') for k, v in team['tracks'].items()})
for team in feed['teams']:
    if team['team'] == 'vayu':
        print('Vayu status:', json.dumps({'watch': team['watch'], 'queue': team['queue'],
              'latest': {k: v.get('latest') for k, v in team['tracks'].items()}}))
