import os
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from fastapi.testclient import TestClient
import app as tasks_api


def test_complete_rest_cycle(tmp_path, monkeypatch):
    monkeypatch.setattr(tasks_api, 'DB_PATH', tmp_path / 'db.sqlite3')
    with TestClient(tasks_api.app) as client:
        # Unauthenticated request -> 401
        assert client.get('/api/v1/tasks').status_code == 401
        assert client.post('/api/v1/auth/login',json={'username':'student','password':'wrong'}).status_code == 401
        token = client.post('/api/v1/auth/login',json={'username':'student','password':'student123'}).json()['access_token']
        auth = {'Authorization': 'Bearer '+token, 'Accept':'application/json'}
        missing = client.get('/api/v1/tasks/56789',headers=auth)
        assert missing.status_code == 404
        assert client.post('/api/v1/tasks',headers=auth,json={'title':''}).status_code == 400
        data = {'title':'Тест', 'description':'Описание', 'status':'new', 'due_date':'2026-12-01',
                'client_uid':'c20015f1-dae8-4d2f-bc68-6ebfe66034ba'}
        created = client.post('/api/v1/tasks',headers=auth,json=data)
        assert created.status_code == 201, created.text
        id_ = created.json()['id']
        repeat = client.post('/api/v1/tasks',headers=auth,json=data)
        assert repeat.status_code == 200 and repeat.json()['id'] == id_
        listing = client.get('/api/v1/tasks',headers=auth)
        assert listing.status_code == 200 and len(listing.json()) == 1
        assert all(name in listing.headers for name in ['etag','cache-control','last-modified'])
        assert client.get('/api/v1/tasks',headers={**auth,'If-None-Match': listing.headers['etag']}).status_code==304
        changed = client.put(f'/api/v1/tasks/{id_}', headers=auth, json={**data, 'status':'done'})
        assert changed.status_code == 200 and changed.json()['status']=='done'
        assert client.get('/api/v1/tasks',headers={**auth,'If-None-Match': listing.headers['etag']}).status_code==200
        guest_token=client.post('/api/v1/auth/login', json={'username':'guest','password':'guest123'}).json()['access_token']
        guest={'Authorization': 'Bearer '+guest_token}
        assert client.get(f'/api/v1/tasks/{id_}', headers=guest).status_code==403
        assert client.delete(f'/api/v1/tasks/{id_}',headers=auth).status_code==204
        assert client.get(f'/api/v1/tasks/{id_}',headers=auth).status_code==404
        assert client.get('/api/v1/debug/error',headers=auth).status_code==403
        monkeypatch.setenv('TASKS_DEMO_ERRORS','1')
        assert client.get('/api/v1/debug/error',headers=auth).status_code==500
        assert client.get('/openapi.json').status_code == 200
        assert client.get('/docs').status_code == 200
        assert client.get('/').status_code == 200
