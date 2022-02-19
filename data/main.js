const getTempCgiPath = 'temp';
const getTempButton = document.getElementById('getTempButton');
const getTempResult = document.getElementById('getTempResult');

const sendIrCgiPath = 'ir/aircon';
const sendIrButton = document.getElementById('sendIrButton');
const sendIrForm = document.getElementById('sendIrForm');
const sendIrResult = document.getElementById('sendIrResult');

const httpOK = 200;

function getTemp() {
	let xhr = new XMLHttpRequest();
	xhr.open('GET', getTempCgiPath);
	xhr.onload = function() {
		getTempResult.style.color = (xhr.status == httpOK) ? null : 'red';
		getTempResult.innerHTML = xhr.response;
	};
	xhr.onerror = function() {
		getTempResult.style.color = 'red';
		getTempResult.innerHTML = 'error';
	};
	getTempResult.innerHTML = '';
	xhr.send();
}

function sendIr() {
	let data = new FormData(sendIrForm);
	let query = new URLSearchParams(data).toString();
	let xhr = new XMLHttpRequest();
	xhr.open('GET', sendIrCgiPath + '?' + query);
	xhr.onload = function() {
		sendIrResult.style.color =  (xhr.status == httpOK) ? null : 'red';
		sendIrResult.innerHTML = xhr.response;
	};
	xhr.onerror = function() {
		sendIrResult.style.color = 'red';
		sendIrResult.innerHTML = 'error';
	};
	sendIrResult.innerHTML = '';
	xhr.send();
}

getTempButton.addEventListener('click', getTemp);
sendIrButton.addEventListener('click', sendIr);
sendIrForm.addEventListener('change', function() {
	sendIrResult.innerHTML = '';
});

getTemp();
