import http from 'k6/http';
import { check } from 'k6';
import { Counter, Trend } from 'k6/metrics';

const TARGET = __ENV.TARGET || 'http://10.0.1.253/';
const VUS = Number(__ENV.VUS || 50);
const DURATION = __ENV.DURATION || '30s';

const backendHits = new Counter('backend_hits');
const ttfb = new Trend('backend_ttfb_ms', true);

export const options = {
  stages: [
    { duration: '10s', target: VUS },
    { duration: DURATION, target: VUS },
    { duration: '5s', target: 0 },
  ],

  noConnectionReuse: __ENV.REUSE !== 'true',
  thresholds: {
    http_req_failed: ['rate<0.01'],
    http_req_duration: ['p(95)<50'],
  },
};

export default function () {
  const res = http.get(TARGET);
  check(res, {
    'status is 200': (r) => r.status === 200,
    'served by a backend': (r) => !!r.body && r.body.includes('Hostname:'),
  });

  const match = res.body && res.body.match(/Hostname:\s*(\S+)/);
  if (match) {
    backendHits.add(1, { backend: match[1] });
  }
  ttfb.add(res.timings.waiting);
}
